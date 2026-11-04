#include <stdlib.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <Autolock.h>
#include <Looper.h>
#include <Message.h>
#include <OS.h>

#include <CodeEditView/TextModel.h>

#include "DebugSession.h"
#include "Documents.h"
#include "Protocol.h"


namespace Debugger {


static constexpr bigtime_t kTimeout = 20000000;


// Keeps the session's notices for the test thread.
class NoticeCollector: public BLooper {
private:
	sem_id fSem;

public:
	std::vector<BMessage> notices;

	NoticeCollector():
		BLooper("NoticeCollector"),
		fSem(create_sem(0, "notices"))
	{
	}

	~NoticeCollector() override
	{
		delete_sem(fSem);
	}

	void MessageReceived(BMessage *message) override
	{
		notices.push_back(*message);
		release_sem(fSem);
	}

	// Waits for a notice matching the predicate after index from; returns
	// its index.
	std::optional<size_t> WaitFor(size_t from, const std::function<bool(const BMessage&)> &predicate)
	{
		const bigtime_t deadline = system_time() + kTimeout;
		for (;;) {
			{
				BAutolock lock(this);
				for (size_t i = from; i < notices.size(); i++) {
					if (predicate(notices[i])) {
						return i;
					}
				}
			}
			if (acquire_sem_etc(fSem, 1, B_ABSOLUTE_TIMEOUT, deadline) != B_OK) {
				return std::nullopt;
			}
		}
	}
};


static bool IsState(const BMessage &message, SessionState state)
{
	return message.what == kNoticeState && message.GetInt32("state", -1) == static_cast<int32>(state);
}


static bool IsStackAt(const BMessage &message, const std::string &path, int32 line)
{
	if (message.what != kNoticeStack) {
		return false;
	}
	const int32 current = message.GetInt32("current", 0);
	return path == message.GetString("path", current, "") && message.GetInt32("line", current, 0) == line;
}


TEST_CASE("debug session with GDB")
{
	const char *program = getenv("DEBUGGER_SAMPLE");
	const char *source = getenv("DEBUGGER_SAMPLE_SOURCE");
	if (program == nullptr || source == nullptr || system("gdb --version > /dev/null 2>&1") != 0) {
		SKIP("GDB or the sample program is not available");
	}

	DebugSession *session = new DebugSession();
	session->Run();
	NoticeCollector *collector = new NoticeCollector();
	collector->Run();
	DocumentRegistry::Default().SetListener(BMessenger(session));

	// A breakpoint on the line calling Area().
	const BReference<SourceDocument> document = DocumentRegistry::Default().Open(source);
	REQUIRE(document != nullptr);
	const std::string path = document->path;
	int32 breakLine = 0;
	{
		CodeEdit::ModelLock lock(*document->model);
		const std::string text = document->model->Text().ToString();
		const size_t offset = text.find("const int area = Area(square);");
		REQUIRE(offset != std::string::npos);
		const size_t line = document->model->Text().LineOfOffset(offset);
		document->model->AddMark(CodeEdit::kMarkBreakpoint, line);
		breakLine = static_cast<int32>(line + 1);
	}

	BMessage watch(kMsgWatch);
	watch.AddMessenger("target", BMessenger(collector));
	BMessenger(session).SendMessage(&watch);

	BMessage launch(kMsgLaunch);
	launch.AddString("program", program);
	launch.AddString("arguments", "one \"two three\"");
	BMessenger(session).SendMessage(&launch);

	const std::optional<size_t> stopped = collector->WaitFor(0, [&](const BMessage &message) {
		return IsStackAt(message, path, breakLine);
	});
	REQUIRE(stopped.has_value());
	{
		CodeEdit::ModelLock lock(*document->model);
		bool execMark = false;
		for (const CodeEdit::MarkInfo &mark : document->model->Marks(breakLine - 1, breakLine - 1)) {
			execMark = execMark || mark.kind == CodeEdit::kMarkExecLine;
		}
		CHECK(execMark);
	}

	CHECK(collector->WaitFor(0, [](const BMessage &message) {
		return message.what == kNoticeBreakpoints && message.GetBool("verified", false);
	}).has_value());

	// Locals of main() contain "square".
	const std::optional<size_t> scopes = collector->WaitFor(*stopped, [](const BMessage &message) {
		return message.what == kNoticeScopes && message.HasString("name");
	});
	REQUIRE(scopes.has_value());
	int64 locals = 0;
	{
		BAutolock lock(collector);
		const BMessage &message = collector->notices[*scopes];
		for (int32 i = 0; message.HasString("name", i); i++) {
			if (std::string(message.GetString("name", i, "")) == "Locals") {
				locals = message.GetInt64("reference", i, 0);
			}
		}
	}
	REQUIRE(locals > 0);
	BMessage variables(kMsgRequestVariables);
	variables.AddInt64("reference", locals);
	BMessenger(session).SendMessage(&variables);
	CHECK(collector->WaitFor(*scopes, [&](const BMessage &message) {
		if (message.what != kNoticeVariables || message.GetInt64("reference", 0) != locals) {
			return false;
		}
		for (int32 i = 0; message.HasString("name", i); i++) {
			if (std::string(message.GetString("name", i, "")) == "square") {
				return true;
			}
		}
		return false;
	}).has_value());

	// Stepping over stops on the next line.
	size_t mark = collector->notices.size();
	BMessenger(session).SendMessage(kMsgStepOver);
	CHECK(collector->WaitFor(mark, [&](const BMessage &message) {
		return IsStackAt(message, path, breakLine + 1);
	}).has_value());

	// GDB reports the worker thread once the program ran again.
	CHECK(collector->WaitFor(0, [](const BMessage &message) {
		int32 count = 0;
		message.GetInfo("id", nullptr, &count);
		return message.what == kNoticeThreads && count >= 2;
	}).has_value());

	mark = collector->notices.size();
	BMessenger(session).SendMessage(kMsgStop);
	CHECK(collector->WaitFor(mark, [](const BMessage &message) {
		return IsState(message, SessionState::Idle);
	}).has_value());
	{
		CodeEdit::ModelLock lock(*document->model);
		for (const CodeEdit::MarkInfo &info : document->model->Marks(0, document->model->Text().NewlineCount())) {
			CHECK(info.kind == CodeEdit::kMarkBreakpoint);
		}
	}

	DocumentRegistry::Default().SetListener(BMessenger());
	session->Lock();
	session->Quit();
	collector->Lock();
	collector->Quit();
}


} // Debugger
