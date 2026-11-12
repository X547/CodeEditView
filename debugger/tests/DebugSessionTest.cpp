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
#include "Document.h"
#include "Protocol.h"


extern char **environ;


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

	Workspace::DocumentRegistry registry;
	DebugSession *session = new DebugSession(registry, 1);
	session->Run();
	NoticeCollector *collector = new NoticeCollector();
	collector->Run();

	// A breakpoint on the line calling Area().
	const BReference<Workspace::Document> document = registry.Open(Workspace::Locator::File(source));
	registry.WaitIdle();
	REQUIRE(document->Status().state == Workspace::DocumentState::Ready);
	const std::string path = document->GetLocator().Path();
	CodeEdit::TextModel &model = *document->Model();
	int32 breakLine = 0;
	{
		CodeEdit::ModelLock lock(model);
		const std::string text = model.Text().ToString();
		const size_t offset = text.find("const int area = Area(square);");
		REQUIRE(offset != std::string::npos);
		const size_t line = model.Text().LineOfOffset(offset);
		model.SharedMarks().AddMark(CodeEdit::kMarkBreakpoint, line);
		breakLine = static_cast<int32>(line + 1);
	}
	const BReference<CodeEdit::MarkSet> sessionMarks = session->MarkSetFor(&model);

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
		// The execution line is a mark of the session, not a shared one.
		CodeEdit::ModelLock lock(model);
		bool execMark = false;
		for (const CodeEdit::MarkInfo &mark : sessionMarks->Marks(breakLine - 1, breakLine - 1)) {
			execMark = execMark || mark.kind == CodeEdit::kMarkExecLine;
		}
		CHECK(execMark);
		for (const CodeEdit::MarkInfo &mark : model.SharedMarks().Marks(0, model.Text().NewlineCount())) {
			CHECK(mark.kind == CodeEdit::kMarkBreakpoint);
		}
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
		CodeEdit::ModelLock lock(model);
		CHECK(sessionMarks->Marks(0, model.Text().NewlineCount()).empty());
		CHECK(model.SharedMarks().Marks(0, model.Text().NewlineCount()).size() == 1);
	}

	session->Lock();
	session->Quit();
	collector->Lock();
	collector->Quit();
}


TEST_CASE("watchers get the output so far and can stop watching")
{
	Workspace::DocumentRegistry registry;
	DebugSession *session = new DebugSession(registry, 7);
	session->Run();
	NoticeCollector *early = new NoticeCollector();
	early->Run();
	NoticeCollector *late = new NoticeCollector();
	late->Run();

	BMessage watch(kMsgWatch);
	watch.AddMessenger("target", BMessenger(early));
	BMessenger(session).SendMessage(&watch);
	// Not running: the session says so in its output.
	BMessage evaluate(kMsgEvaluate);
	evaluate.AddString("expression", "print 1");
	BMessenger(session).SendMessage(&evaluate);
	REQUIRE(early->WaitFor(0, [](const BMessage &message) {
		return message.what == kNoticeOutput && message.HasString("text");
	}).has_value());

	// A later watcher gets that output at once, marked with the session.
	BMessage lateWatch(kMsgWatch);
	lateWatch.AddMessenger("target", BMessenger(late));
	BMessenger(session).SendMessage(&lateWatch);
	CHECK(late->WaitFor(0, [](const BMessage &message) {
		return message.what == kNoticeOutput && message.GetInt32("session", 0) == 7
			&& std::string(message.GetString("text", "")).find("Not running") != std::string::npos;
	}).has_value());

	// After unwatching, no more notices.
	BMessage unwatch(kMsgUnwatch);
	unwatch.AddMessenger("target", BMessenger(early));
	BMessenger(session).SendMessage(&unwatch);
	size_t earlyCount = 0;
	{
		BAutolock lock(early);
		earlyCount = early->notices.size();
	}
	BMessenger(session).SendMessage(&evaluate);
	const size_t lateMark = late->notices.size();
	CHECK(late->WaitFor(lateMark, [](const BMessage &message) {
		return message.what == kNoticeOutput;
	}).has_value());
	{
		BAutolock lock(early);
		CHECK(early->notices.size() == earlyCount);
	}

	session->Lock();
	session->Quit();
	early->Lock();
	early->Quit();
	late->Lock();
	late->Quit();
}


// The session's output, for failures.
static std::string Outputs(NoticeCollector &collector)
{
	BAutolock lock(&collector);
	std::string text;
	for (const BMessage &message : collector.notices) {
		if (message.what == kNoticeOutput) {
			text += message.GetString("text", "");
		} else if (message.what == kNoticeState) {
			text += std::string("[state] ") + message.GetString("text", "") + "\n";
		}
	}
	return text;
}


TEST_CASE("breakpoints set before attaching")
{
	const char *program = getenv("DEBUGGER_SAMPLE");
	const char *source = getenv("DEBUGGER_SAMPLE_SOURCE");
	if (program == nullptr || source == nullptr || system("gdb --version > /dev/null 2>&1") != 0) {
		SKIP("GDB or the sample program is not available");
	}

	// The program runs on its own first.
	const char *arguments[] = {program, nullptr};
	const thread_id team = load_image(1, arguments, const_cast<const char**>(environ));
	REQUIRE(team >= 0);
	resume_thread(team);
	snooze(500000);

	Workspace::DocumentRegistry registry;
	DebugSession *session = new DebugSession(registry, 1);
	session->Run();
	NoticeCollector *collector = new NoticeCollector();
	collector->Run();

	const BReference<Workspace::Document> document = registry.Open(Workspace::Locator::File(source));
	registry.WaitIdle();
	REQUIRE(document->Status().state == Workspace::DocumentState::Ready);
	const std::string path = document->GetLocator().Path();
	CodeEdit::TextModel &model = *document->Model();
	int32 breakLine = 0;
	{
		CodeEdit::ModelLock lock(model);
		const size_t offset = model.Text().ToString().find("const int area = Area(square);");
		REQUIRE(offset != std::string::npos);
		const size_t line = model.Text().LineOfOffset(offset);
		model.SharedMarks().AddMark(CodeEdit::kMarkBreakpoint, line);
		breakLine = static_cast<int32>(line + 1);
	}
	const BReference<CodeEdit::MarkSet> sessionMarks = session->MarkSetFor(&model);

	BMessage watch(kMsgWatch);
	watch.AddMessenger("target", BMessenger(collector));
	BMessenger(session).SendMessage(&watch);

	BMessage attach(kMsgAttach);
	attach.AddInt32("team", team);
	BMessenger(session).SendMessage(&attach);

	// Attaching stops the program; it goes on with the breakpoint set, and
	// passes the line once a second.
	const std::optional<size_t> attached = collector->WaitFor(0, [](const BMessage &message) {
		return IsState(message, SessionState::Stopped);
	});
	INFO(Outputs(*collector));
	REQUIRE(attached.has_value());
	CHECK(collector->WaitFor(0, [](const BMessage &message) {
		return message.what == kNoticeBreakpoints && message.GetBool("verified", false);
	}).has_value());
	BMessenger(session).SendMessage(kMsgContinue);
	const std::optional<size_t> stopped = collector->WaitFor(*attached + 1, [&](const BMessage &message) {
		return IsStackAt(message, path, breakLine);
	});
	INFO(Outputs(*collector));
	CHECK(stopped.has_value());

	const size_t mark = collector->notices.size();
	BMessenger(session).SendMessage(kMsgStop);
	CHECK(collector->WaitFor(mark, [](const BMessage &message) {
		return IsState(message, SessionState::Idle);
	}).has_value());
	kill_team(team);

	session->Lock();
	session->Quit();
	collector->Lock();
	collector->Quit();
}


TEST_CASE("two debug sessions at once")
{
	const char *program = getenv("DEBUGGER_SAMPLE");
	const char *source = getenv("DEBUGGER_SAMPLE_SOURCE");
	if (program == nullptr || source == nullptr || system("gdb --version > /dev/null 2>&1") != 0) {
		SKIP("GDB or the sample program is not available");
	}

	Workspace::DocumentRegistry registry;
	const BReference<Workspace::Document> document = registry.Open(Workspace::Locator::File(source));
	registry.WaitIdle();
	REQUIRE(document->Status().state == Workspace::DocumentState::Ready);
	const std::string path = document->GetLocator().Path();
	CodeEdit::TextModel &model = *document->Model();
	int32 breakLine = 0;
	{
		CodeEdit::ModelLock lock(model);
		const size_t offset = model.Text().ToString().find("const int area = Area(square);");
		REQUIRE(offset != std::string::npos);
		const size_t line = model.Text().LineOfOffset(offset);
		model.SharedMarks().AddMark(CodeEdit::kMarkBreakpoint, line);
		breakLine = static_cast<int32>(line + 1);
	}

	// Both stop at the shared breakpoint, each in its own program.
	DebugSession *sessions[2] {};
	NoticeCollector *collectors[2] {};
	for (int i = 0; i < 2; i++) {
		sessions[i] = new DebugSession(registry, i + 1);
		sessions[i]->Run();
		collectors[i] = new NoticeCollector();
		collectors[i]->Run();
		BMessage watch(kMsgWatch);
		watch.AddMessenger("target", BMessenger(collectors[i]));
		BMessenger(sessions[i]).SendMessage(&watch);
		BMessage launch(kMsgLaunch);
		launch.AddString("program", program);
		BMessenger(sessions[i]).SendMessage(&launch);
	}
	for (int i = 0; i < 2; i++) {
		const std::optional<size_t> stopped = collectors[i]->WaitFor(0, [&](const BMessage &message) {
			return IsStackAt(message, path, breakLine);
		});
		INFO(Outputs(*collectors[i]));
		CHECK(stopped.has_value());
	}

	// Stopping one leaves the other stopped at its breakpoint.
	const size_t mark = collectors[0]->notices.size();
	BMessenger(sessions[0]).SendMessage(kMsgStop);
	CHECK(collectors[0]->WaitFor(mark, [](const BMessage &message) {
		return IsState(message, SessionState::Idle);
	}).has_value());
	{
		BAutolock lock(collectors[1]);
		bool stillStopped = false;
		for (const BMessage &message : collectors[1]->notices) {
			if (message.what == kNoticeState) {
				stillStopped = message.GetInt32("state", -1) == static_cast<int32>(SessionState::Stopped);
			}
		}
		CHECK(stillStopped);
	}
	const size_t otherMark = collectors[1]->notices.size();
	BMessenger(sessions[1]).SendMessage(kMsgStop);
	CHECK(collectors[1]->WaitFor(otherMark, [](const BMessage &message) {
		return IsState(message, SessionState::Idle);
	}).has_value());

	for (int i = 0; i < 2; i++) {
		sessions[i]->Lock();
		sessions[i]->Quit();
		collectors[i]->Lock();
		collectors[i]->Quit();
	}
}


} // Debugger
