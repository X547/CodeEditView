#include "DebugSession.h"

#include <algorithm>

#include <Application.h>
#include <Autolock.h>
#include <Path.h>


namespace Debugger {


static constexpr uint32 kMsgSyncBreakpoints = 'DSsb';
static constexpr uint32 kMsgRefreshThreads = 'DSrt';
static constexpr int32 kMaxFrames = 200;


static std::vector<std::string> SplitArguments(const std::string &text)
{
	std::vector<std::string> arguments;
	std::string current;
	bool quoted = false;
	bool hasCurrent = false;
	for (const char ch : text) {
		if (ch == '"') {
			quoted = !quoted;
			hasCurrent = true;
		} else if ((ch == ' ' || ch == '\t') && !quoted) {
			if (hasCurrent) {
				arguments.push_back(current);
				current.clear();
				hasCurrent = false;
			}
		} else {
			current += ch;
			hasCurrent = true;
		}
	}
	if (hasCurrent) {
		arguments.push_back(current);
	}
	return arguments;
}


DebugSession::DebugSession(Workspace::DocumentRegistry &registry, int32 id, Workspace::PathMapper mapper):
	BLooper("DebugSession"),
	fRegistry(registry),
	fId(id),
	fMapper(std::move(mapper)),
	fMarkLock("DebugSession marks")
{
	fRegistry.AddListener(BMessenger(this));
	// The documents open already.
	PostMessage(Workspace::kNoticeDocumentChanged);
}


DebugSession::~DebugSession()
{
	fRegistry.RemoveListener(BMessenger(this));
	if (fClient != nullptr) {
		fClient->Stop();
	}
	for (auto &[locator, watched] : fDocuments) {
		CodeEdit::ModelLock lock(*watched.model);
		watched.model->RemoveObserver(watched.observer);
	}
}


BReference<CodeEdit::MarkSet> DebugSession::MarkSetFor(CodeEdit::TextModel *model)
{
	BAutolock lock(fMarkLock);
	BReference<CodeEdit::MarkSet> &set = fMarkSets[model];
	if (set.Get() == nullptr) {
		set.SetTo(new CodeEdit::MarkSet(*model), true);
	}
	return set;
}


// Sets that only the session holds and that have no marks are forgotten.
void DebugSession::DropUnusedMarkSets()
{
	BAutolock lock(fMarkLock);
	std::erase_if(fMarkSets, [this](const auto &item) {
		const BReference<CodeEdit::MarkSet> &set = item.second;
		return set->CountReferences() == 1
			&& !(fExecMark.has_value() && fExecMark->set == set)
			&& !(fFrameMark.has_value() && fFrameMark->set == set);
	});
}


bool DebugSession::QuitRequested()
{
	if (fClient != nullptr) {
		fClient->Send("disconnect", JsonValue::MakeObject().Set("terminateDebuggee", !fAttached));
	}
	End();
	return true;
}


//#pragma mark - Protocol

void DebugSession::Request(const std::string &command, JsonValue arguments, ResponseHandler handler)
{
	if (fClient == nullptr) {
		return;
	}
	const int64_t seq = fClient->Send(command, std::move(arguments));
	if (seq >= 0) {
		fPending[seq] = {command, std::move(handler)};
	}
}


void DebugSession::HandleMessage(const std::string &json)
{
	const std::optional<JsonValue> message = JsonValue::Parse(json);
	if (!message.has_value()) {
		Output("Invalid message from GDB: " + json + "\n", "important");
		return;
	}
	const std::string &type = (*message)["type"].AsString();
	if (type == "event") {
		HandleEvent((*message)["event"].AsString(), (*message)["body"]);
		return;
	}
	if (type != "response") {
		return;
	}
	const auto it = fPending.find((*message)["request_seq"].AsInt());
	if (it == fPending.end()) {
		return;
	}
	const auto [command, handler] = std::move(it->second);
	fPending.erase(it);
	if (!(*message)["success"].AsBool()) {
		const std::string &error = (*message)["message"].AsString();
		Output(command + ": " + (error.empty() ? "failed" : error) + "\n", "important");
	}
	if (handler) {
		handler(*message);
	}
}


void DebugSession::HandleEvent(const std::string &event, const JsonValue &body)
{
	if (event == "initialized") {
		fInitialized = true;
		MaybeConfigure();
	} else if (event == "output") {
		const std::string &category = body["category"].AsString();
		if (category != "telemetry") {
			Output(body["output"].AsString(), category.empty() ? "console" : category.c_str());
		}
	} else if (event == "stopped") {
		if (body.Has("threadId")) {
			fCurrentThread = body["threadId"].AsInt();
		}
		std::string status = "Stopped";
		const std::string &reason = body["reason"].AsString();
		if (!reason.empty()) {
			status += ": " + reason;
		}
		const std::string &description = body["description"].AsString();
		if (!description.empty() && description != reason) {
			status += " (" + description + ")";
		}
		ClearStopState();
		SetState(SessionState::Stopped, status);
		RefreshThreads();
		RequestStack(true);
	} else if (event == "continued") {
		if (fState == SessionState::Stopped) {
			ClearStopState();
			SetState(SessionState::Running, "Running");
		}
	} else if (event == "thread") {
		if (!fThreadsRefreshPosted) {
			fThreadsRefreshPosted = true;
			PostMessage(kMsgRefreshThreads);
		}
	} else if (event == "breakpoint") {
		const JsonValue &breakpoint = body["breakpoint"];
		const int64_t id = breakpoint["id"].AsInt(-1);
		for (auto &[path, statuses] : fBreakpointStatus) {
			for (BreakpointStatus &status : statuses) {
				if (status.id == id) {
					status.verified = breakpoint["verified"].AsBool();
					status.message = breakpoint["message"].AsString();
				}
			}
		}
		BMessage notice = BreakpointsNotice();
		Broadcast(notice);
	} else if (event == "exited") {
		Output("Program exited with code " + std::to_string(body["exitCode"].AsInt()) + "\n", "important");
	} else if (event == "terminated") {
		End();
	}
}


//#pragma mark - Session control

void DebugSession::Start(const std::string &command, JsonValue arguments, std::vector<std::string> gdbArguments, const std::string &status)
{
	if (fClient != nullptr) {
		fClient->Send("disconnect", JsonValue::MakeObject().Set("terminateDebuggee", !fAttached));
		End();
	}
	fClient = std::make_unique<DapClient>(BMessenger(this));
	std::vector<std::string> argv {"gdb", "-q", "-i=dap"};
	argv.insert(argv.end(), gdbArguments.begin(), gdbArguments.end());
	if (fClient->Start(argv) != B_OK) {
		fClient.reset();
		Output("Cannot start gdb\n", "important");
		return;
	}
	fAttached = command == "attach";
	fInitialized = false;
	fStartSent = false;
	fConfigured = false;
	// Workaround for a bug of GDB on Haiku (17.2): haiku_nat_target::attach
	// does not disable breakpoints until the main executable is relocated
	// (create_inferior does). A breakpoint that exists while GDB attaches
	// resolves to its offset in the executable file, inserting it there
	// fails, and the error leaves post_create_inferior before the
	// inferior_created observer relocates the executable: later breakpoints
	// get file addresses too, and the stop is not reported. Breakpoints set
	// after the attach are right, so they are sent once it finished.
	fBreakpointsHeld = fAttached;
	SetState(SessionState::Starting, status);

	JsonValue initialize;
	initialize.Set("adapterID", "gdb");
	initialize.Set("clientName", "CodeEdit");
	initialize.Set("linesStartAt1", true);
	initialize.Set("columnsStartAt1", true);
	initialize.Set("pathFormat", "path");
	initialize.Set("supportsVariableType", true);
	Request("initialize", initialize, [this, command, arguments](const JsonValue &response) {
		if (!response["success"].AsBool()) {
			End();
			return;
		}
		// GDB answers launch and attach only after configurationDone.
		Request(command, arguments, [this](const JsonValue &startResponse) {
			if (!startResponse["success"].AsBool()) {
				End();
				return;
			}
			if (fBreakpointsHeld) {
				fBreakpointsHeld = false;
				SyncBreakpoints(true);
			}
		});
		fStartSent = true;
		MaybeConfigure();
	});
}


// Breakpoints are set between the initialized event and configurationDone.
void DebugSession::MaybeConfigure()
{
	if (!fInitialized || !fStartSent || fConfigured) {
		return;
	}
	fConfigured = true;
	SyncBreakpoints(true);
	Request("configurationDone", JsonValue::MakeObject());
	SetState(SessionState::Running, fAttached ? "Attached" : "Running");
}


void DebugSession::End()
{
	if (fClient != nullptr) {
		fClient->Stop();
		fClient.reset();
	}
	fPending.clear();
	fConfigured = false;
	fThreads.clear();
	fCurrentThread = 0;
	ClearStopState();
	for (auto &[locator, watched] : fDocuments) {
		watched.sentLines.reset();
	}
	fBreakpointStatus.clear();
	SetState(SessionState::Idle, "Not running");
	BMessage threads = ThreadsNotice();
	Broadcast(threads);
	BMessage breakpoints = BreakpointsNotice();
	Broadcast(breakpoints);
}


void DebugSession::Resume(const char *command)
{
	if (fState != SessionState::Stopped) {
		return;
	}
	const uint64_t generation = fStopGeneration;
	Request(command, JsonValue::MakeObject().Set("threadId", fCurrentThread),
		[this, generation](const JsonValue &response) {
			if (response["success"].AsBool() && generation == fStopGeneration
				&& fState == SessionState::Stopped) {
				ClearStopState();
				SetState(SessionState::Running, "Running");
			}
		});
}


void DebugSession::SetState(SessionState state, const std::string &status)
{
	fState = state;
	fStatus = status;
	BMessage notice = StateNotice();
	Broadcast(notice);
}


void DebugSession::ClearStopState()
{
	fStopGeneration++;
	fFrames.clear();
	fCurrentFrame = 0;
	fScopes.clear();
	RemoveMark(fExecMark);
	RemoveMark(fFrameMark);
	DropUnusedMarkSets();
	BMessage stack = StackNotice();
	Broadcast(stack);
	BMessage scopes = ScopesNotice();
	Broadcast(scopes);
}


void DebugSession::RefreshThreads()
{
	Request("threads", JsonValue::MakeObject(), [this](const JsonValue &response) {
		if (!response["success"].AsBool()) {
			return;
		}
		fThreads.clear();
		for (const JsonValue &thread : response["body"]["threads"].AsArray()) {
			fThreads.push_back({thread["id"].AsInt(), thread["name"].AsString()});
		}
		if (fCurrentThread == 0 && !fThreads.empty()) {
			fCurrentThread = fThreads.front().id;
		}
		BMessage notice = ThreadsNotice();
		Broadcast(notice);
	});
}


// Loads the current thread's stack and selects its first frame with source.
void DebugSession::RequestStack(bool showSource)
{
	JsonValue arguments;
	arguments.Set("threadId", fCurrentThread);
	arguments.Set("startFrame", 0);
	arguments.Set("levels", kMaxFrames);
	const uint64_t generation = fStopGeneration;
	Request("stackTrace", arguments, [this, generation, showSource](const JsonValue &response) {
		if (generation != fStopGeneration || !response["success"].AsBool()) {
			return;
		}
		fFrames.clear();
		for (const JsonValue &frame : response["body"]["stackFrames"].AsArray()) {
			FrameInfo info;
			info.id = frame["id"].AsInt();
			info.function = frame["name"].AsString();
			info.module = frame["moduleId"].AsString();
			info.path = frame["source"]["path"].AsString();
			info.line = static_cast<int32>(frame["line"].AsInt());
			fFrames.push_back(std::move(info));
		}
		RemoveMark(fExecMark);
		if (!fFrames.empty() && !fFrames.front().path.empty()) {
			PlaceMark(fExecMark, CodeEdit::kMarkExecLine, fFrames.front().path, fFrames.front().line);
		}
		int32 index = 0;
		for (size_t i = 0; i < fFrames.size(); i++) {
			if (!fFrames[i].path.empty() && OpenDocument(fFrames[i].path).Get() != nullptr) {
				index = static_cast<int32>(i);
				break;
			}
		}
		SelectFrame(index, showSource);
	});
}


void DebugSession::SelectFrame(int32 index, bool showSource)
{
	if (index < 0 || static_cast<size_t>(index) >= fFrames.size()) {
		return;
	}
	fCurrentFrame = index;
	const FrameInfo &frame = fFrames[index];
	RemoveMark(fFrameMark);
	if (index > 0 && !frame.path.empty()) {
		PlaceMark(fFrameMark, kMarkFrameLine, frame.path, frame.line);
	}
	if (showSource && !frame.path.empty() && be_app != nullptr) {
		if (const std::optional<Workspace::Locator> locator = fMapper.ToLocator(frame.path)) {
			BMessage show(kMsgShowSource);
			show.AddString("locator", locator->ToString().c_str());
			show.AddInt32("line", frame.line);
			show.AddInt32("session", fId);
			be_app->PostMessage(&show);
		}
	}
	BMessage stack = StackNotice();
	Broadcast(stack);

	fScopes.clear();
	const uint64_t generation = fStopGeneration;
	Request("scopes", JsonValue::MakeObject().Set("frameId", frame.id), [this, generation](const JsonValue &response) {
		if (generation != fStopGeneration || !response["success"].AsBool()) {
			return;
		}
		fScopes.clear();
		for (const JsonValue &scope : response["body"]["scopes"].AsArray()) {
			fScopes.push_back({scope["name"].AsString(), scope["variablesReference"].AsInt()});
		}
		BMessage notice = ScopesNotice();
		Broadcast(notice);
	});
}


void DebugSession::RequestVariables(int64_t reference)
{
	if (fState != SessionState::Stopped || reference <= 0) {
		return;
	}
	const uint64_t generation = fStopGeneration;
	Request("variables", JsonValue::MakeObject().Set("variablesReference", reference),
		[this, generation, reference](const JsonValue &response) {
			if (generation != fStopGeneration || !response["success"].AsBool()) {
				return;
			}
			BMessage notice(kNoticeVariables);
			notice.AddInt64("reference", reference);
			for (const JsonValue &variable : response["body"]["variables"].AsArray()) {
				notice.AddString("name", variable["name"].AsString().c_str());
				notice.AddString("value", variable["value"].AsString().c_str());
				notice.AddString("type", variable["type"].AsString().c_str());
				notice.AddInt64("child", variable["variablesReference"].AsInt());
			}
			Broadcast(notice);
		});
}


void DebugSession::Output(const std::string &text, const char *category)
{
	BMessage notice(kNoticeOutput);
	notice.AddString("text", text.c_str());
	notice.AddString("category", category);
	Broadcast(notice);
}


//#pragma mark - Breakpoints

// The document of a GDB path, read; nullptr if it cannot be.
BReference<Workspace::Document> DebugSession::OpenDocument(const std::string &path)
{
	const std::optional<Workspace::Locator> locator = fMapper.ToLocator(path);
	if (!locator.has_value()) {
		return {};
	}
	BReference<Workspace::Document> document = fRegistry.Open(*locator);
	if (document->Status().state == Workspace::DocumentState::Loading) {
		fRegistry.WaitIdle();
	}
	if (document->Status().state != Workspace::DocumentState::Ready) {
		return {};
	}
	return document;
}


// Follows the breakpoints of the registry's documents that GDB can know.
// Documents closed by the registry are let go.
void DebugSession::WatchDocuments()
{
	std::map<Workspace::Locator, BReference<Workspace::Document>> current;
	for (const BReference<Workspace::Document> &document : fRegistry.All()) {
		if (document->Status().state == Workspace::DocumentState::Ready) {
			current.emplace(document->GetLocator(), document);
		}
	}
	for (auto it = fDocuments.begin(); it != fDocuments.end();) {
		const auto found = current.find(it->first);
		if (found != current.end() && found->second->Model() == it->second.model.Get()) {
			++it;
			continue;
		}
		{
			CodeEdit::ModelLock lock(*it->second.model);
			it->second.model->RemoveObserver(it->second.observer);
		}
		it = fDocuments.erase(it);
	}
	for (const auto &[locator, document] : current) {
		if (fDocuments.contains(locator)) {
			continue;
		}
		const std::optional<std::string> path = fMapper.ToToolPath(locator);
		if (!path.has_value()) {
			continue;
		}
		WatchedDocument &watched = fDocuments[locator];
		watched.model.SetTo(document->Model());
		watched.path = *path;
		CodeEdit::ModelLock lock(*watched.model);
		watched.observer = watched.model->AddObserver(BMessenger(this));
	}
}


void DebugSession::DocumentChanged(CodeEdit::TextModel *model)
{
	for (auto &[locator, watched] : fDocuments) {
		if (watched.model.Get() != model) {
			continue;
		}
		CodeEdit::ModelChanges changes;
		{
			CodeEdit::ModelLock lock(*model);
			changes = model->Sync(watched.observer);
		}
		if ((changes.overlaysChanged || !changes.edits.empty() || changes.reset) && !fBreakpointSyncPosted) {
			fBreakpointSyncPosted = true;
			PostMessage(kMsgSyncBreakpoints);
		}
		return;
	}
}


std::vector<int32> DebugSession::EnabledBreakpointLines(CodeEdit::TextModel &model)
{
	std::vector<int32> lines;
	CodeEdit::ModelLock lock(model);
	for (const CodeEdit::MarkInfo &mark : model.SharedMarks().Marks(0, model.Text().NewlineCount())) {
		if (mark.kind == CodeEdit::kMarkBreakpoint) {
			lines.push_back(static_cast<int32>(mark.line + 1));
		}
	}
	std::sort(lines.begin(), lines.end());
	lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
	return lines;
}


// Sends the breakpoints of documents whose enabled breakpoint lines changed
// (all with force) and tells the watchers.
void DebugSession::SyncBreakpoints(bool force)
{
	for (auto &[locator, watched] : fDocuments) {
		if (!fConfigured || fClient == nullptr || fBreakpointsHeld) {
			break;
		}
		std::vector<int32> lines = EnabledBreakpointLines(*watched.model);
		// Documents never sent without breakpoints need no request.
		const bool changed = watched.sentLines.has_value()
			? lines.size() != watched.sentLines->size()
				|| !std::equal(lines.begin(), lines.end(), watched.sentLines->begin())
			: !lines.empty();
		if (!changed && !(force && watched.sentLines.has_value())) {
			continue;
		}
		watched.sentLines = lines;
		SendBreakpoints(watched.path, std::move(lines));
	}
	BMessage notice = BreakpointsNotice();
	Broadcast(notice);
}


void DebugSession::SendBreakpoints(const std::string &path, std::vector<int32> lines)
{
	JsonValue breakpoints = JsonValue::MakeArray();
	for (const int32 line : lines) {
		breakpoints.Append(JsonValue::MakeObject().Set("line", line));
	}
	JsonValue arguments;
	arguments.Set("source", JsonValue::MakeObject().Set("path", path));
	arguments.Set("breakpoints", std::move(breakpoints));
	Request("setBreakpoints", arguments, [this, path, lines](const JsonValue &response) {
		std::vector<BreakpointStatus> &statuses = fBreakpointStatus[path];
		statuses.clear();
		const JsonValue &results = response["body"]["breakpoints"];
		for (size_t i = 0; i < lines.size(); i++) {
			BreakpointStatus status;
			status.requestedLine = lines[i];
			status.id = results[i]["id"].AsInt(-1);
			status.verified = results[i]["verified"].AsBool();
			status.message = results[i]["message"].AsString();
			if (status.message.empty()) {
				status.message = results[i]["reason"].AsString();
			}
			statuses.push_back(std::move(status));
		}
		BMessage notice = BreakpointsNotice();
		Broadcast(notice);
	});
}


void DebugSession::PlaceMark(std::optional<PlacedMark> &mark, uint32_t kind, const std::string &path, int32 line)
{
	RemoveMark(mark);
	const BReference<Workspace::Document> document = OpenDocument(path);
	if (document.Get() == nullptr || line <= 0) {
		return;
	}
	CodeEdit::TextModel &model = *document->Model();
	BReference<CodeEdit::MarkSet> set = MarkSetFor(&model);
	CodeEdit::ModelLock lock(model);
	const size_t markLine = std::min<size_t>(line - 1, model.Text().NewlineCount());
	mark = PlacedMark {set, set->AddMark(kind, markLine, 0, CodeEdit::MarkDeletePolicy::MoveToEditLine)};
}


void DebugSession::RemoveMark(std::optional<PlacedMark> &mark)
{
	if (!mark.has_value()) {
		return;
	}
	{
		CodeEdit::ModelLock lock(mark->set->Model());
		mark->set->RemoveMark(mark->id);
	}
	mark.reset();
}


//#pragma mark - Notices

void DebugSession::Broadcast(BMessage &message)
{
	for (auto it = fWatchers.begin(); it != fWatchers.end();) {
		if (it->SendMessage(&message) == B_BAD_PORT_ID) {
			it = fWatchers.erase(it);
		} else {
			++it;
		}
	}
}


BMessage DebugSession::StateNotice() const
{
	BMessage notice(kNoticeState);
	notice.AddInt32("state", static_cast<int32>(fState));
	notice.AddString("text", fStatus.c_str());
	return notice;
}


BMessage DebugSession::ThreadsNotice() const
{
	BMessage notice(kNoticeThreads);
	for (const ThreadInfo &thread : fThreads) {
		notice.AddInt64("id", thread.id);
		notice.AddString("name", thread.name.c_str());
	}
	notice.AddInt64("current", fCurrentThread);
	return notice;
}


BMessage DebugSession::StackNotice() const
{
	BMessage notice(kNoticeStack);
	for (const FrameInfo &frame : fFrames) {
		notice.AddString("function", frame.function.c_str());
		notice.AddString("module", frame.module.c_str());
		notice.AddString("path", frame.path.c_str());
		notice.AddInt32("line", frame.line);
	}
	notice.AddInt32("current", fCurrentFrame);
	return notice;
}


BMessage DebugSession::ScopesNotice() const
{
	BMessage notice(kNoticeScopes);
	for (const ScopeInfo &scope : fScopes) {
		notice.AddString("name", scope.name.c_str());
		notice.AddInt64("reference", scope.reference);
	}
	return notice;
}


BMessage DebugSession::BreakpointsNotice()
{
	BMessage notice(kNoticeBreakpoints);
	for (auto &[locator, watched] : fDocuments) {
		std::vector<CodeEdit::MarkInfo> marks;
		{
			CodeEdit::TextModel &model = *watched.model;
			CodeEdit::ModelLock lock(model);
			marks = model.SharedMarks().Marks(0, model.Text().NewlineCount());
		}
		const std::string &path = watched.path;
		const std::vector<BreakpointStatus> &statuses = fBreakpointStatus[path];
		for (const CodeEdit::MarkInfo &mark : marks) {
			const bool enabled = mark.kind == CodeEdit::kMarkBreakpoint;
			if (!enabled && mark.kind != CodeEdit::kMarkBreakpointDisabled) {
				continue;
			}
			const int32 line = static_cast<int32>(mark.line + 1);
			bool verified = false;
			std::string message;
			if (enabled) {
				for (const BreakpointStatus &status : statuses) {
					if (status.requestedLine == line) {
						verified = status.verified;
						message = status.message;
					}
				}
			}
			notice.AddString("path", path.c_str());
			notice.AddString("locator", locator.ToString().c_str());
			notice.AddInt32("line", line);
			notice.AddBool("enabled", enabled);
			notice.AddBool("verified", verified);
			notice.AddString("message", message.c_str());
		}
	}
	return notice;
}


//#pragma mark - Messages

void DebugSession::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgDapMessage:
			HandleMessage(message->GetString("json", ""));
			break;
		case kMsgDapClosed:
			if (fClient != nullptr) {
				End();
			}
			break;
		case CodeEdit::kMsgModelChanged: {
			void *model = nullptr;
			if (message->FindPointer("model", &model) == B_OK) {
				DocumentChanged(static_cast<CodeEdit::TextModel*>(model));
			}
			break;
		}
		case Workspace::kNoticeDocumentChanged:
			WatchDocuments();
			SyncBreakpoints(false);
			break;
		case kMsgSyncBreakpoints:
			fBreakpointSyncPosted = false;
			SyncBreakpoints(false);
			break;
		case kMsgRefreshThreads:
			fThreadsRefreshPosted = false;
			if (fState == SessionState::Running || fState == SessionState::Stopped) {
				RefreshThreads();
			}
			break;
		case kMsgLaunch: {
			const std::string programText = message->GetString("program", "");
			if (programText.empty()) {
				break;
			}
			const std::string program = Workspace::Locator::File(programText).Path();
			JsonValue arguments;
			arguments.Set("program", program);
			JsonValue args = JsonValue::MakeArray();
			for (const std::string &argument : SplitArguments(message->GetString("arguments", ""))) {
				args.Append(argument);
			}
			arguments.Set("args", std::move(args));
			BPath directory(program.c_str());
			if (directory.GetParent(&directory) == B_OK) {
				arguments.Set("cwd", directory.Path());
			}
			if (message->GetBool("stopAtMain", false)) {
				arguments.Set("stopAtBeginningOfMainSubprogram", true);
			}
			// The program on GDB's command line loads its symbols before
			// the breakpoints are set.
			Start("launch", arguments, {program}, "Starting " + program);
			break;
		}
		case kMsgAttach: {
			const int32 team = message->GetInt32("team", -1);
			if (team < 0) {
				break;
			}
			Start("attach", JsonValue::MakeObject().Set("pid", team), {}, "Attaching to team " + std::to_string(team));
			break;
		}
		case kMsgContinue:
			Resume("continue");
			break;
		case kMsgStepOver:
			Resume("next");
			break;
		case kMsgStepInto:
			Resume("stepIn");
			break;
		case kMsgStepOut:
			Resume("stepOut");
			break;
		case kMsgPause:
			if (fState == SessionState::Running) {
				const int64_t thread = fCurrentThread != 0
					? fCurrentThread
					: (fThreads.empty() ? 1 : fThreads.front().id);
				Request("pause", JsonValue::MakeObject().Set("threadId", thread));
			}
			break;
		case kMsgStop:
			if (fClient != nullptr) {
				Request("disconnect", JsonValue::MakeObject().Set("terminateDebuggee", !fAttached),
					[this](const JsonValue &) {
						End();
					});
			}
			break;
		case kMsgSelectThread:
			if (fState == SessionState::Stopped) {
				fCurrentThread = message->GetInt64("thread", fCurrentThread);
				ClearStopState();
				BMessage notice = ThreadsNotice();
				Broadcast(notice);
				RequestStack(true);
			}
			break;
		case kMsgSelectFrame:
			if (fState == SessionState::Stopped) {
				SelectFrame(message->GetInt32("frame", 0), message->GetBool("show", true));
			}
			break;
		case kMsgRequestVariables:
			RequestVariables(message->GetInt64("reference", 0));
			break;
		case kMsgEvaluate: {
			const std::string expression = message->GetString("expression", "");
			if (fClient == nullptr) {
				Output("Not running\n", "important");
				break;
			}
			Output("(gdb) " + expression + "\n", "input");
			JsonValue arguments;
			arguments.Set("expression", expression);
			arguments.Set("context", "repl");
			if (fState == SessionState::Stopped && static_cast<size_t>(fCurrentFrame) < fFrames.size()) {
				arguments.Set("frameId", fFrames[fCurrentFrame].id);
			}
			Request("evaluate", arguments, [this](const JsonValue &response) {
				const std::string &result = response["body"]["result"].AsString();
				if (!result.empty()) {
					Output(result + "\n");
				}
			});
			break;
		}
		case kMsgWatch: {
			BMessenger target;
			if (message->FindMessenger("target", &target) != B_OK) {
				break;
			}
			fWatchers.push_back(target);
			BMessage state = StateNotice();
			target.SendMessage(&state);
			BMessage threads = ThreadsNotice();
			target.SendMessage(&threads);
			BMessage stack = StackNotice();
			target.SendMessage(&stack);
			BMessage scopes = ScopesNotice();
			target.SendMessage(&scopes);
			BMessage breakpoints = BreakpointsNotice();
			target.SendMessage(&breakpoints);
			break;
		}
		default:
			BLooper::MessageReceived(message);
			break;
	}
}


} // Debugger
