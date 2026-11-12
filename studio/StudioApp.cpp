#include "StudioApp.h"

#include <string.h>

#include <algorithm>

#include <Alert.h>
#include <Autolock.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Path.h>
#include <WindowStack.h>

#include "CodeEditLink.h"
#include "Common.h"
#include "ControlWindow.h"
#include "CoverageMessages.h"
#include "CoverageRun.h"
#include "CoverageSummaryWindow.h"
#include "DebugSession.h"
#include "DocumentsWindow.h"
#include "Messages.h"
#include "OpenGitWindow.h"
#include "OpenLocationWindow.h"
#include "OpenWindow.h"
#include "SourceWindow.h"
#include "WindowHelpers.h"


namespace Studio {


static constexpr const char *kAppSignature = DiffKit::kCodeEditSignature;


static BRect NextWindowFrame()
{
	static int32 count = 0;
	const float offset = 24 * (count++ % 8);
	return BRect(200 + offset, 80 + offset, 999 + offset, 779 + offset);
}


StudioApp::StudioApp():
	BApplication(kAppSignature),
	fWatcher(new Workspace::DocumentWatcher(fRegistry)),
	fWindowLock("StudioApp windows")
{
	fWatcher->Run();
}


StudioApp::~StudioApp()
{
	QuitSessions();
	fWatcher->Lock();
	fWatcher->Quit();
	delete fOpenPanel;
}


void AddRevealFields(BMessage &message, const ShowOptions &options)
{
	message.AddInt32("line", options.line);
	message.AddInt32("column", options.column);
	message.AddBool("goTo", options.goTo);
}


void ReadRevealFields(const BMessage &message, ShowOptions &options)
{
	options.line = message.GetInt32("line", 0);
	options.column = message.GetInt32("column", 0);
	options.goTo = message.GetBool("goTo", false);
}


StudioApp &StudioApp::Get()
{
	return *static_cast<StudioApp*>(be_app);
}


void StudioApp::ShowDocument(const Workspace::Locator &locator, BWindow *origin, const ShowOptions &options)
{
	BReference<Workspace::Document> document = fRegistry.Open(locator);
	fWatcher->Update();
	BMessage reveal(kMsgReveal);
	AddRevealFields(reveal, options);
	if (!options.newView) {
		BAutolock lock(fWindowLock);
		for (const WindowEntry &entry : fWindows) {
			if (entry.locator == document->GetLocator()) {
				entry.messenger.SendMessage(&reveal);
				return;
			}
		}
	}

	const bool stack = origin != nullptr && Stacking();
	SourceWindow *window = new SourceWindow(document, stack ? origin->Frame() : NextWindowFrame(), options.session,
		options.coverage);
	window->Show();
	if (options.line > 0) {
		window->PostMessage(&reveal);
	}
	if (stack) {
		BWindowStack(origin).AddWindow(BMessenger(window));
	}
}


void StudioApp::NewDocument(BWindow *origin)
{
	ShowOptions options;
	options.newView = true;
	ShowDocument(fRegistry.NewUntitled()->GetLocator(), origin, options);
}


std::optional<Workspace::Locator> StudioApp::ChosenLocator(const BMessage &message)
{
	entry_ref directory;
	const char *name = nullptr;
	if (message.FindRef("directory", &directory) != B_OK || message.FindString("name", &name) != B_OK) {
		return std::nullopt;
	}
	BPath path(&directory);
	if (path.InitCheck() != B_OK || path.Append(name) != B_OK) {
		return std::nullopt;
	}
	return Workspace::Locator::File(path.Path());
}


void StudioApp::WindowShows(const BMessenger &window, const Workspace::Locator &locator, int32 session,
	int32 coverage)
{
	{
		BAutolock lock(fWindowLock);
		const auto it = std::find_if(fWindows.begin(), fWindows.end(), [&](const WindowEntry &entry) {
			return entry.messenger == window;
		});
		if (it != fWindows.end()) {
			it->locator = locator;
			it->session = session;
			it->coverage = coverage;
		} else {
			fWindows.push_back({window, locator, session, coverage});
		}
	}
	fWatcher->Update();
}


void StudioApp::WindowClosing(const BMessenger &window)
{
	{
		BAutolock lock(fWindowLock);
		std::erase_if(fWindows, [&](const WindowEntry &entry) {
			return entry.messenger == window;
		});
	}
	PostMessage(kMsgWindowClosed);
}


void StudioApp::DocumentsWindowClosing()
{
	{
		BAutolock lock(fWindowLock);
		fDocumentsWindow = BMessenger();
	}
	PostMessage(kMsgWindowClosed);
}


int32 StudioApp::CountWindowsShowing(const Workspace::Locator &locator)
{
	BAutolock lock(fWindowLock);
	return std::count_if(fWindows.begin(), fWindows.end(), [&](const WindowEntry &entry) {
		return entry.locator == locator;
	});
}


bool StudioApp::Stacking()
{
	BAutolock lock(fWindowLock);
	return fStacking;
}


void StudioApp::SetStacking(bool stacking)
{
	BAutolock lock(fWindowLock);
	fStacking = stacking;
}


// The frame of the window a request comes from, if it says.
static std::optional<BRect> OpenerFrame(const BMessage &message)
{
	BRect frame;
	if (message.FindRect("openerFrame", &frame) != B_OK) {
		return std::nullopt;
	}
	return frame;
}


//#pragma mark - Debug sessions

int32 StudioApp::NewSession(BRect openerFrame)
{
	BAutolock lock(fWindowLock);
	const int32 id = fNextSessionId++;
	SessionEntry &entry = fSessions[id];
	entry.name = "Session " + std::to_string(id);
	entry.session = new Debugger::DebugSession(fRegistry, id);
	entry.session->Run();

	Debugger::SessionContext context;
	context.session = BMessenger(entry.session);
	context.id = id;
	context.titleSuffix = " \xe2\x80\x94 " + entry.name;
	context.registry = &fRegistry;
	// The console collects all output from the start.
	entry.tools[Debugger::ToolWindowKind::Console] = Debugger::CreateToolWindow(Debugger::ToolWindowKind::Console,
		context);
	BMessage closeRequest(kMsgCloseSession);
	closeRequest.AddInt32("session", id);
	entry.control = new Debugger::ControlWindow(context, ("Debug" + context.titleSuffix).c_str(), "", "",
		closeRequest);
	DiffKit::PlaceOver(entry.control, openerFrame);
	entry.control->Show();
	return id;
}


std::vector<SessionInfo> StudioApp::Sessions()
{
	BAutolock lock(fWindowLock);
	std::vector<SessionInfo> sessions;
	for (const auto &[id, entry] : fSessions) {
		sessions.push_back({id, entry.name, BMessenger(entry.session)});
	}
	return sessions;
}


BReference<CodeEdit::MarkSet> StudioApp::SessionMarks(int32 session, CodeEdit::TextModel *model)
{
	BAutolock lock(fWindowLock);
	const auto it = fSessions.find(session);
	if (it == fSessions.end()) {
		return {};
	}
	return it->second.session->MarkSetFor(model);
}


void StudioApp::ShowToolWindow(int32 session, Debugger::ToolWindowKind kind, std::optional<BRect> openerFrame)
{
	BWindow *window = nullptr;
	{
		BAutolock lock(fWindowLock);
		const auto it = fSessions.find(session);
		if (it == fSessions.end()) {
			return;
		}
		SessionEntry &entry = it->second;
		BWindow *&tool = entry.tools[kind];
		if (tool == nullptr) {
			Debugger::SessionContext context;
			context.session = BMessenger(entry.session);
			context.id = session;
			context.titleSuffix = " \xe2\x80\x94 " + entry.name;
			context.registry = &fRegistry;
			tool = Debugger::CreateToolWindow(kind, context);
		}
		window = tool;
	}
	if (window == nullptr || !window->Lock()) {
		return;
	}
	if (window->IsHidden()) {
		if (openerFrame.has_value()) {
			DiffKit::PlaceOver(window, *openerFrame);
		}
		window->Show();
	}
	window->Activate();
	window->Unlock();
}


// Shows the document in a window following the session (or else the
// coverage run) of the options: one showing the document, else a new one in
// the stack of a window following it, else a new window.
void StudioApp::ShowFollowing(const Workspace::Locator &requested, const ShowOptions &options)
{
	const Workspace::Locator locator = fRegistry.Open(requested)->GetLocator();
	BMessenger target;
	bool sameDocument = false;
	{
		BAutolock lock(fWindowLock);
		for (const WindowEntry &entry : fWindows) {
			const bool follows = options.session != 0
				? entry.session == options.session
				: entry.coverage == options.coverage;
			if (!follows) {
				continue;
			}
			if (entry.locator == locator) {
				target = entry.messenger;
				sameDocument = true;
				break;
			}
			if (!target.IsValid()) {
				target = entry.messenger;
			}
		}
	}
	if (sameDocument) {
		BMessage reveal(kMsgReveal);
		AddRevealFields(reveal, options);
		target.SendMessage(&reveal);
		return;
	}
	ShowOptions newWindow = options;
	newWindow.newView = true;
	if (target.IsValid()) {
		BMessage open(kMsgOpenLocator);
		open.AddString("locator", locator.ToString().c_str());
		open.AddBool("newView", true);
		open.AddInt32("session", options.session);
		open.AddInt32("coverage", options.coverage);
		AddRevealFields(open, options);
		target.SendMessage(&open);
		return;
	}
	ShowDocument(locator, nullptr, newWindow);
}


void StudioApp::BroadcastToSourceWindows(BMessage &message)
{
	std::vector<BMessenger> windows;
	{
		BAutolock lock(fWindowLock);
		for (const WindowEntry &window : fWindows) {
			windows.push_back(window.messenger);
		}
	}
	for (const BMessenger &window : windows) {
		window.SendMessage(&message);
	}
}


void StudioApp::CloseSession(int32 id)
{
	SessionEntry entry;
	{
		BAutolock lock(fWindowLock);
		const auto it = fSessions.find(id);
		if (it == fSessions.end()) {
			return;
		}
		entry = it->second;
		fSessions.erase(it);
	}
	for (auto &[kind, tool] : entry.tools) {
		if (tool != nullptr && tool->Lock()) {
			tool->Quit();
		}
	}
	if (entry.control->Lock()) {
		entry.control->Quit();
	}
	// Ends the program (QuitRequested() disconnects).
	BMessenger(entry.session).SendMessage(B_QUIT_REQUESTED);
	BMessage closed(kMsgSessionClosed);
	closed.AddInt32("session", id);
	BroadcastToSourceWindows(closed);
	PostMessage(kMsgWindowClosed);
}


// Sessions and coverage runs, when the application quits.
void StudioApp::QuitSessions()
{
	std::map<int32, SessionEntry> sessions;
	std::map<int32, CoverageEntry> coverages;
	{
		BAutolock lock(fWindowLock);
		sessions = std::move(fSessions);
		fSessions.clear();
		coverages = std::move(fCoverages);
		fCoverages.clear();
	}
	for (auto &[id, entry] : sessions) {
		if (entry.session->Lock()) {
			entry.session->Quit();
		}
	}
	for (auto &[id, entry] : coverages) {
		BMessenger(entry.run).SendMessage(B_QUIT_REQUESTED);
	}
}


//#pragma mark - Coverage runs

void StudioApp::OpenCoverage(const std::string &binary, const std::string &profile)
{
	BAutolock lock(fWindowLock);
	const int32 id = fNextCoverageId++;
	CoverageEntry &entry = fCoverages[id];
	entry.run = new CoverageRun(id, binary, profile);
	entry.run->Run();
	entry.summary = new CoverageSummaryWindow(entry.run);
	entry.summary->Show();
}


void StudioApp::CloseCoverage(int32 id)
{
	CoverageEntry entry;
	{
		BAutolock lock(fWindowLock);
		const auto it = fCoverages.find(id);
		if (it == fCoverages.end()) {
			return;
		}
		entry = it->second;
		fCoverages.erase(it);
	}
	if (entry.summary->Lock()) {
		entry.summary->Quit();
	}
	BMessage closed(kMsgCoverageClosed);
	closed.AddInt32("coverage", id);
	BroadcastToSourceWindows(closed);
	// The run may be busy with llvm-cov.
	BMessenger(entry.run).SendMessage(B_QUIT_REQUESTED);
	PostMessage(kMsgWindowClosed);
}


std::vector<CoverageInfo> StudioApp::Coverages()
{
	BAutolock lock(fWindowLock);
	std::vector<CoverageInfo> coverages;
	for (const auto &[id, entry] : fCoverages) {
		coverages.push_back({id, entry.run->Name()});
	}
	return coverages;
}


BReference<CodeEdit::DecorationSet> StudioApp::CoverageDecorations(int32 coverage, const Workspace::Locator &locator,
	CodeEdit::TextModel *model)
{
	BAutolock lock(fWindowLock);
	const auto it = fCoverages.find(coverage);
	if (it == fCoverages.end()) {
		return {};
	}
	return it->second.run->DecorationsFor(locator, model);
}


std::string StudioApp::CoverageStatus(int32 coverage, const Workspace::Locator &locator)
{
	BAutolock lock(fWindowLock);
	const auto it = fCoverages.find(coverage);
	if (it == fCoverages.end()) {
		return {};
	}
	return it->second.run->Name() + ": " + it->second.run->FileStatus(locator);
}


void StudioApp::WatchCoverage(int32 coverage, const BMessenger &watcher, bool watch)
{
	BAutolock lock(fWindowLock);
	const auto it = fCoverages.find(coverage);
	if (it == fCoverages.end()) {
		return;
	}
	if (watch) {
		it->second.run->AddWatcher(watcher);
	} else {
		it->second.run->RemoveWatcher(watcher);
	}
}


//#pragma mark - Windows

void StudioApp::ShowDocumentsWindow(std::optional<BRect> openerFrame)
{
	BAutolock lock(fWindowLock);
	if (fDocumentsWindow.IsValid()) {
		fDocumentsWindow.SendMessage(kMsgActivate);
		return;
	}
	DocumentsWindow *window = new DocumentsWindow();
	if (openerFrame.has_value()) {
		DiffKit::PlaceOver(window, *openerFrame);
	}
	fDocumentsWindow = BMessenger(window);
	window->Show();
}


void StudioApp::ShowOpenFilePanel()
{
	if (fOpenPanel == nullptr) {
		fOpenPanel = new BFilePanel(B_OPEN_PANEL, nullptr, nullptr, B_FILE_NODE, true);
	}
	fOpenPanel->Show();
}


// A locator string, or a path (relative to cwd).
void StudioApp::OpenArgument(const std::string &argument, const char *cwd)
{
	std::optional<Workspace::Locator> locator;
	if (argument.find("://") != std::string::npos) {
		locator = Workspace::Locator::Parse(argument);
	} else if (argument.starts_with('/') || cwd == nullptr) {
		locator = Workspace::Locator::File(argument);
	} else {
		locator = Workspace::Locator::File(std::string(cwd) + "/" + argument);
	}
	if (!locator.has_value()) {
		(new BAlert("CodeEdit", ("Not a location: " + argument).c_str(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL,
			B_STOP_ALERT))->Go(nullptr);
		return;
	}
	ShowDocument(*locator, nullptr);
}


// CodeEdit [--coverage binary profile] [path or locator...]
void StudioApp::ArgvReceived(int32 argc, char **argv)
{
	const char *cwd = CurrentMessage()->GetString("cwd", nullptr);
	const auto absolute = [cwd](const char *path) {
		return path[0] == '/' || cwd == nullptr ? std::string(path) : std::string(cwd) + "/" + path;
	};
	for (int32 i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--coverage") == 0 && i + 2 < argc) {
			OpenCoverage(absolute(argv[i + 1]), absolute(argv[i + 2]));
			i += 2;
			continue;
		}
		OpenArgument(argv[i], cwd);
	}
}


// Files opened with the application (Tracker, BRoster::Launch(), /bin/open
// as Terminal does for "file:line:column"). With "be:line" and optional
// "be:column" (int32, 1-based) the caret goes there, as for other editors.
void StudioApp::RefsReceived(BMessage *message)
{
	ShowOptions options;
	options.line = message->GetInt32("be:line", 0);
	options.column = message->GetInt32("be:column", 0);
	options.goTo = options.line > 0;
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		// Symbolic links are opened as their targets' files.
		BEntry entry(&ref, true);
		BPath path(&entry);
		if (path.InitCheck() != B_OK || entry.IsDirectory()) {
			continue;
		}
		ShowDocument(Workspace::Locator::File(path.Path()), nullptr, options);
	}
}


void StudioApp::ReadyToRun()
{
	BAutolock lock(fWindowLock);
	if (fWindows.empty() && fCoverages.empty()) {
		lock.Unlock();
		ShowDocumentsWindow();
	}
}


void StudioApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgOpenLocator: {
			const std::optional<Workspace::Locator> locator
				= Workspace::Locator::Parse(message->GetString("locator", ""));
			if (locator.has_value()) {
				ShowOptions options;
				options.newView = message->GetBool("newView", false);
				options.session = message->GetInt32("session", 0);
				options.coverage = message->GetInt32("coverage", 0);
				ReadRevealFields(*message, options);
				ShowDocument(*locator, nullptr, options);
			}
			break;
		}
		case Debugger::kMsgShowSource:
		case kMsgShowCoverageSource: {
			const std::optional<Workspace::Locator> locator
				= Workspace::Locator::Parse(message->GetString("locator", ""));
			if (locator.has_value()) {
				ShowOptions options;
				options.session = message->GetInt32("session", 0);
				options.coverage = message->GetInt32("coverage", 0);
				options.line = message->GetInt32("line", 0);
				ShowFollowing(*locator, options);
			}
			break;
		}
		case Coverage::kMsgOpenCoverage:
			OpenCoverage(message->GetString("binary", ""), message->GetString("profile", ""));
			break;
		case kMsgShowOpenCoverage: {
			BWindow *window = new Coverage::OpenWindow(message->GetString("binary", ""), "");
			if (const std::optional<BRect> frame = OpenerFrame(*message)) {
				DiffKit::PlaceOver(window, *frame);
			}
			window->Show();
			break;
		}
		case kMsgCloseCoverage:
			CloseCoverage(message->GetInt32("coverage", 0));
			break;
		case kMsgShowCoverageSummary: {
			BWindow *summary = nullptr;
			{
				BAutolock lock(fWindowLock);
				const auto it = fCoverages.find(message->GetInt32("coverage", 0));
				if (it != fCoverages.end()) {
					summary = it->second.summary;
				}
			}
			if (summary != nullptr && summary->Lock()) {
				summary->Activate();
				summary->Unlock();
			}
			break;
		}
		case Debugger::kMsgShowToolWindow:
			ShowToolWindow(message->GetInt32("session", 0),
				static_cast<Debugger::ToolWindowKind>(message->GetInt32("window", 0)), OpenerFrame(*message));
			break;
		case kMsgCloseSession:
			CloseSession(message->GetInt32("session", 0));
			break;
		case kMsgShowSessionControl: {
			BWindow *control = nullptr;
			{
				BAutolock lock(fWindowLock);
				const auto it = fSessions.find(message->GetInt32("session", 0));
				if (it != fSessions.end()) {
					control = it->second.control;
				}
			}
			if (control != nullptr && control->Lock()) {
				control->Activate();
				control->Unlock();
			}
			break;
		}
		case kMsgShowOpenFile:
			ShowOpenFilePanel();
			break;
		case kMsgNewDocument:
			NewDocument(nullptr);
			break;
		case DiffKit::kMsgCodeEditOpenGitFile: {
			ShowOptions options;
			options.line = message->GetInt32("line", 0);
			options.column = message->GetInt32("column", 0);
			options.goTo = options.line > 0;
			ShowDocument(Workspace::Locator::GitCommit(message->GetString("repository", ""),
				message->GetString("commit", ""), message->GetString("path", "")), nullptr, options);
			break;
		}
		case kMsgShowOpenGit:
			(new OpenGitWindow(BMessenger(this), message->GetString("directory", "")))->Show();
			break;
		case kMsgShowOpenLocation:
			(new OpenLocationWindow(BMessenger(this)))->Show();
			break;
		case kMsgShowDocuments:
			ShowDocumentsWindow(OpenerFrame(*message));
			break;
		case kMsgToggleStacking:
			SetStacking(!Stacking());
			break;
		case kMsgWindowClosed: {
			fRegistry.CloseUnused();
			fWatcher->Update();
			BAutolock lock(fWindowLock);
			// Documents may have closed.
			fDocumentsWindow.SendMessage(kMsgWindowClosed);
			if (fWindows.empty() && !fDocumentsWindow.IsValid() && fSessions.empty() && fCoverages.empty()) {
				PostMessage(B_QUIT_REQUESTED);
			}
			break;
		}
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


bool StudioApp::QuitRequested()
{
	// Tool windows close instead of hiding.
	Debugger::gQuitting = true;
	if (!BApplication::QuitRequested()) {
		Debugger::gQuitting = false;
		return false;
	}
	QuitSessions();
	return true;
}


} // Studio


int main()
{
	Studio::StudioApp app;
	app.Run();
	return 0;
}
