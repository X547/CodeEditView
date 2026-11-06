#include "LlvmCov.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include <stdexcept>
#include <system_error>
#include <utility>

#include <OS.h>
#include <image.h>


namespace Coverage {


// Closes the descriptor on destruction unless released.
class FileDescriptor {
private:
	int fFd = -1;

public:
	explicit FileDescriptor(int fd = -1): fFd(fd) {}
	~FileDescriptor() {Close();}

	FileDescriptor(const FileDescriptor &other) = delete;
	FileDescriptor &operator=(const FileDescriptor &other) = delete;

	int Get() const {return fFd;}

	void Close()
	{
		if (fFd >= 0) {
			close(fFd);
			fFd = -1;
		}
	}

	void Reset(int fd)
	{
		Close();
		fFd = fd;
	}
};


static void MakePipe(FileDescriptor &readEnd, FileDescriptor &writeEnd)
{
	int fds[2];
	if (pipe(fds) != 0) {
		throw std::system_error(errno, std::generic_category(), "pipe");
	}
	readEnd.Reset(fds[0]);
	writeEnd.Reset(fds[1]);
}


// The child's ends are inherited and must not be stdin, stdout or stderr
// already (the redirections would mix them up).
static void MoveAboveStdio(FileDescriptor &fd)
{
	if (fd.Get() > STDERR_FILENO) {
		return;
	}
	const int moved = fcntl(fd.Get(), F_DUPFD, STDERR_FILENO + 1);
	if (moved < 0) {
		throw std::system_error(errno, std::generic_category(), "fcntl");
	}
	fd.Reset(moved);
}


// Reads both pipes to their end; they may fill in any order.
static void ReadAll(FileDescriptor &out, FileDescriptor &err, std::string &output, std::string &errors)
{
	pollfd fds[2] = {{out.Get(), POLLIN, 0}, {err.Get(), POLLIN, 0}};
	std::string *targets[2] = {&output, &errors};
	int open = 2;
	while (open > 0) {
		if (poll(fds, 2, -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			throw std::system_error(errno, std::generic_category(), "poll");
		}
		for (int i = 0; i < 2; i++) {
			if (fds[i].fd < 0 || fds[i].revents == 0) {
				continue;
			}
			char buffer[65536];
			const ssize_t size = read(fds[i].fd, buffer, sizeof(buffer));
			if (size > 0) {
				targets[i]->append(buffer, size);
			} else if (size == 0 || errno != EINTR) {
				fds[i].fd = -1;
				open--;
			}
		}
	}
	out.Close();
	err.Close();
}


std::string RunProgram(const std::vector<std::string> &argv)
{
	FileDescriptor outRead, outWrite, errRead, errWrite;
	MakePipe(outRead, outWrite);
	MakePipe(errRead, errWrite);
	fcntl(outRead.Get(), F_SETFD, FD_CLOEXEC);
	fcntl(errRead.Get(), F_SETFD, FD_CLOEXEC);
	MoveAboveStdio(outWrite);
	MoveAboveStdio(errWrite);

	// No fork() (see DapClient::Start()): a shell started by load_image()
	// redirects the inherited pipe ends and replaces itself with the program.
	const std::string out = std::to_string(outWrite.Get());
	const std::string err = std::to_string(errWrite.Get());
	const std::string script = "exec \"$0\" \"$@\" </dev/null >&" + out + " 2>&" + err + " " + out + ">&- " + err + ">&-";
	std::vector<const char*> args {"/bin/sh", "-c", script.c_str()};
	for (const std::string &arg: argv) {
		args.push_back(arg.c_str());
	}
	args.push_back(nullptr);

	const thread_id team = load_image(
		static_cast<int32>(args.size() - 1),
		args.data(),
		const_cast<const char**>(environ)
	);
	outWrite.Close();
	errWrite.Close();
	if (team < 0) {
		throw std::runtime_error(std::string("Cannot run the shell: ") + strerror(team));
	}
	resume_thread(team);

	std::string output;
	std::string errors;
	ReadAll(outRead, errRead, output, errors);
	status_t exitCode = B_OK;
	wait_for_thread(team, &exitCode);
	if (exitCode != 0) {
		if (errors.empty()) {
			errors = argv[0] + " failed with exit code " + std::to_string(exitCode);
		}
		while (!errors.empty() && errors.back() == '\n') {
			errors.pop_back();
		}
		throw std::runtime_error(errors);
	}
	return output;
}


LlvmCov::LlvmCov(std::string binary, std::string profile):
	fBinary(std::move(binary)),
	fProfile(std::move(profile))
{
}


Summary LlvmCov::LoadSummary() const
{
	return ParseSummary(RunProgram({
		"llvm-cov", "export", "-format=text", "-summary-only", fBinary, "-instr-profile=" + fProfile
	}));
}


FileCoverage LlvmCov::LoadFile(const std::string &path) const
{
	return ParseFileCoverage(
		RunProgram({
			"llvm-cov", "export", "-format=text", "-skip-functions", fBinary, "-instr-profile=" + fProfile, path
		}),
		path
	);
}


} // Coverage
