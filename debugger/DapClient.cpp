#include "DapClient.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <Autolock.h>
#include <Message.h>


namespace Debugger {


static constexpr std::string_view kContentLength = "Content-Length:";
static constexpr bigtime_t kStopTimeout = 2000000;


std::optional<std::string> DapFrameReader::Next()
{
	for (;;) {
		const size_t headerEnd = fBuffer.find("\r\n\r\n");
		if (headerEnd == std::string::npos) {
			return std::nullopt;
		}
		// Find the length among the header lines; other output (e.g. from
		// the adapter before it speaks the protocol) is dropped.
		const size_t lengthPos = fBuffer.rfind(kContentLength, headerEnd);
		if (lengthPos == std::string::npos) {
			fBuffer.erase(0, headerEnd + 4);
			continue;
		}
		const size_t length = strtoul(fBuffer.c_str() + lengthPos + kContentLength.size(), nullptr, 10);
		const size_t bodyStart = headerEnd + 4;
		if (fBuffer.size() < bodyStart + length) {
			return std::nullopt;
		}
		std::string body = fBuffer.substr(bodyStart, length);
		fBuffer.erase(0, bodyStart + length);
		return body;
	}
}


std::string DapFrame(std::string_view body)
{
	return std::string(kContentLength) + " " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}


//#pragma mark - DapClient

DapClient::DapClient(const BMessenger &target):
	fTarget(target),
	fWriteLock("DapClient write")
{
}


DapClient::~DapClient()
{
	Stop();
}


status_t DapClient::Start(const std::vector<std::string> &argv)
{
	if (IsRunning() || argv.empty()) {
		return B_NOT_ALLOWED;
	}
	int toChild[2];
	int fromChild[2];
	if (pipe(toChild) != 0) {
		return errno;
	}
	if (pipe(fromChild) != 0) {
		const status_t error = errno;
		close(toChild[0]);
		close(toChild[1]);
		return error;
	}

	std::vector<char*> args;
	for (const std::string &arg : argv) {
		args.push_back(const_cast<char*>(arg.c_str()));
	}
	args.push_back(nullptr);

	const pid_t pid = fork();
	if (pid < 0) {
		const status_t error = errno;
		close(toChild[0]);
		close(toChild[1]);
		close(fromChild[0]);
		close(fromChild[1]);
		return error;
	}
	if (pid == 0) {
		dup2(toChild[0], STDIN_FILENO);
		dup2(fromChild[1], STDOUT_FILENO);
		close(toChild[0]);
		close(toChild[1]);
		close(fromChild[0]);
		close(fromChild[1]);
		// A new process group, so signals for the debugger do not reach it.
		setpgid(0, 0);
		execvp(args[0], args.data());
		_exit(127);
	}

	close(toChild[0]);
	close(fromChild[1]);
	fcntl(toChild[1], F_SETFD, FD_CLOEXEC);
	fcntl(fromChild[0], F_SETFD, FD_CLOEXEC);
	fPid = pid;
	fInput = toChild[1];
	fOutput = fromChild[0];
	fNextSeq = 1;

	fReader = spawn_thread(ReaderEntry, "DAP reader", B_NORMAL_PRIORITY, this);
	if (fReader < 0) {
		const status_t error = fReader;
		Stop();
		return error;
	}
	resume_thread(fReader);
	return B_OK;
}


status_t DapClient::ReaderEntry(void *data)
{
	static_cast<DapClient*>(data)->ReaderLoop();
	return B_OK;
}


void DapClient::ReaderLoop()
{
	DapFrameReader reader;
	char buffer[16384];
	for (;;) {
		const ssize_t count = read(fOutput, buffer, sizeof(buffer));
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			break;
		}
		reader.Append(std::string_view(buffer, count));
		while (std::optional<std::string> body = reader.Next()) {
			BMessage message(kMsgDapMessage);
			message.AddString("json", body->c_str());
			fTarget.SendMessage(&message);
		}
	}
	BMessage closed(kMsgDapClosed);
	fTarget.SendMessage(&closed);
}


int64_t DapClient::Send(const std::string &command, JsonValue arguments)
{
	BAutolock lock(fWriteLock);
	if (fInput < 0) {
		return -1;
	}
	const int64_t seq = fNextSeq++;
	JsonValue request = JsonValue::MakeObject();
	request.Set("seq", seq);
	request.Set("type", "request");
	request.Set("command", command);
	request.Set("arguments", std::move(arguments));
	const std::string frame = DapFrame(request.Serialize());

	size_t written = 0;
	while (written < frame.size()) {
		const ssize_t count = write(fInput, frame.data() + written, frame.size() - written);
		if (count < 0 && errno == EINTR) {
			continue;
		}
		if (count <= 0) {
			return -1;
		}
		written += count;
	}
	return seq;
}


void DapClient::Stop()
{
	if (fPid < 0) {
		return;
	}
	{
		BAutolock lock(fWriteLock);
		close(fInput);
		fInput = -1;
	}
	// The adapter ends when its input closes; force it after a while.
	const bigtime_t deadline = system_time() + kStopTimeout;
	int status;
	while (waitpid(fPid, &status, WNOHANG) == 0) {
		if (system_time() > deadline) {
			kill(-fPid, SIGKILL);
			kill(fPid, SIGKILL);
			waitpid(fPid, &status, 0);
			break;
		}
		snooze(20000);
	}
	fPid = -1;
	if (fReader >= 0) {
		// A process started by the adapter may still hold the pipe open.
		status_t result;
		if (wait_for_thread_etc(fReader, B_RELATIVE_TIMEOUT, kStopTimeout, &result) != B_OK) {
			kill_thread(fReader);
		}
		fReader = -1;
	}
	close(fOutput);
	fOutput = -1;
}


} // Debugger
