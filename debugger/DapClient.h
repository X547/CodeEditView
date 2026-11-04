#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <OS.h>

#include "Json.h"


namespace Debugger {


// Posted to the target: "json" (string) is one protocol message.
constexpr uint32 kMsgDapMessage = 'DAPm';
// Posted to the target when the adapter closed its output.
constexpr uint32 kMsgDapClosed = 'DAPc';


// Splits a stream into Debug Adapter Protocol messages framed by
// "Content-Length: n\r\n\r\n" headers.
class DapFrameReader {
private:
	std::string fBuffer;

public:
	void Append(std::string_view data) {fBuffer.append(data);}
	// The next complete message body, if any. Unframed lines before a header
	// are skipped.
	std::optional<std::string> Next();
};


std::string DapFrame(std::string_view body);


// Runs a debug adapter (e.g. "gdb -i=dap") and exchanges messages with it
// over pipes. Messages arrive as BMessages from a reader thread.
class DapClient {
private:
	BMessenger fTarget;
	pid_t fPid = -1;
	int fInput = -1;
	int fOutput = -1;
	thread_id fReader = -1;
	BLocker fWriteLock;
	int64_t fNextSeq = 1;

	static status_t ReaderEntry(void *data);
	void ReaderLoop();

public:
	explicit DapClient(const BMessenger &target);
	~DapClient();

	DapClient(const DapClient &other) = delete;
	DapClient &operator=(const DapClient &other) = delete;

	status_t Start(const std::vector<std::string> &argv);
	bool IsRunning() const {return fPid >= 0;}
	// Returns the request's sequence number, or -1 if not running.
	int64_t Send(const std::string &command, JsonValue arguments = JsonValue::MakeObject());
	// Closes the pipes and ends the adapter.
	void Stop();
};


} // Debugger
