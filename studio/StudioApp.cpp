#include "StudioApp.h"

#include <algorithm>

#include <Alert.h>
#include <Autolock.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Path.h>
#include <WindowStack.h>

#include "Common.h"
#include "ControlWindow.h"
#include "DebugSession.h"
#include "DocumentsWindow.h"
#include "Messages.h"
#include "OpenGitWindow.h"
#include "OpenLocationWindow.h"
#include "SourceWindow.h"


namespace Studio {


static constexpr const char *kAppSignature = "application/x-vnd.CodeEdit";


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


StudioApp &StudioApp::Get()
{
	return *static_cast<StudioApp*>(be_app);
}


void StudioApp::ShowDocument(const Workspace::Locator &locator, BWindow *origin, bool newView, int32 session,
	int32 line)
{
	BReference<Workspace::Document> document = fRegistry.Open(locator);
	fWatcher->Update();
	if (!newView) {
		BAutolock lock(fWindowLock);
		for (const WindowEntry &entry : fWindows) {
			if (entry.locator == document->GetLocator()) {
				BMessage reveal(kMsgReveal);
				reveal.AddInt32("line", line);
				entry.messenger.SendMessage(&reveal);
				return;
			}
		}
	}

	const bool stack = origin != nullptr && Stacking();
	SourceWindow *window = new SourceWindow(document, stack ? origin->Frame() : NextWindowFrame(), session);
	WindowShows(BMessenger(window), document->GetLocator(), session);
	window->Show();
	if (line > 0) {
		BMessage reveal(kMsgReveal);
		reveal.AddInt32("line", line);
		window->PostMessage(&reveal);
	}
	if (stack) {
		BWindowStack(origin).AddWindow(BMessenger(window));
	}
}


void StudioApp::WindowShows(const BMessenger &window, const Workspace::Locator &locator, int32 session)
{
	{
		BAutolock lock(fWindowLock);
		const auto it = std::find_if(fWindows.begin(), fWindows.end(), [&](const WindowEntry &entry) {
			return entry.messenger == window;
		});
		if (it != fWindows.end()) {
			it->locator = locator;
			it->session = session;
		} else {
			fWindows.push_back({window, locator, session});
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


//#pragma mark - Debug sessions

int32 StudioApp::NewSession()
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


void StudioApp::ShowToolWindow(int32 session, Debugger::ToolWindowKind kind)
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
		window->Show();
	}
	window->Activate();
	window->Unlock();
}


// Shows the line in a window following the session: one showing the
// document, else a new one in the stack of a window following the session.
void StudioApp::ShowSource(const Workspace::Locator &requested, int32 line, int32 session)
{
	const Workspace::Locator locator = fRegistry.Open(requested)->GetLocator();
	BMessenger target;
	bool sameDocument = false;
	{
		BAutolock lock(fWindowLock);
		for (const WindowEntry &entry : fWindows) {
			if (entry.session != session) {
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
		reveal.AddInt32("line", line);
		target.SendMessage(&reveal);
		return;
	}
	if (target.IsValid()) {
		BMessage open(kMsgOpenLocator);
		open.AddString("locator", locator.ToString().c_str());
		open.AddBool("newView", true);
		open.AddInt32("session", session);
		open.AddInt32("line", line);
		target.SendMessage(&open);
		return;
	}
	ShowDocument(locator, nullptr, true, session, line);
}


void StudioApp::CloseSession(int32 id)
{
	SessionEntry entry;
	std::vector<BMessenger> windows;
	{
		BAutolock lock(fWindowLock);
		const auto it = fSessions.find(id);
		if (it == fSessions.end()) {
			return;
		}
		entry = it->second;
		fSessions.erase(it);
		for (const WindowEntry &window : fWindows) {
			windows.push_back(window.messenger);
		}
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
	for (const BMessenger &window : windows) {
		window.SendMessage(&closed);
	}
}


void StudioApp::QuitSessions()
{
	std::map<int32, SessionEntry> sessions;
	{
		BAutolock lock(fWindowLock);
		sessions = std::move(fSessions);
		fSessions.clear();
	}
	for (auto &[id, entry] : sessions) {
		if (entry.session->Lock()) {
			entry.session->Quit();
		}
	}
}


//#pragma mark - Windows

void StudioApp::ShowDocumentsWindow()
{
	BAutolock lock(fWindowLock);
	if (fDocumentsWindow.IsValid()) {
		fDocumentsWindow.SendMessage(kMsgActivate);
		return;
	}
	DocumentsWindow *window = new DocumentsWindow();
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


// CodeEdit [path or locator...]
void StudioApp::ArgvReceived(int32 argc, char **argv)
{
	const char *cwd = CurrentMessage()->GetString("cwd", nullptr);
	for (int32 i = 1; i < argc; i++) {
		OpenArgument(argv[i], cwd);
	}
}


void StudioApp::RefsReceived(BMessage *message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BPath path(&ref);
		if (path.InitCheck() == B_OK) {
			ShowDocument(Workspace::Locator::File(path.Path()), nullptr);
		}
	}
}


void StudioApp::ReadyToRun()
{
	BAutolock lock(fWindowLock);
	if (fWindows.empty()) {
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
				ShowDocument(*locator, nullptr, message->GetBool("newView", false), message->GetInt32("session", 0),
					message->GetInt32("line", 0));
			}
			break;
		}
		case Debugger::kMsgShowSource: {
			const std::optional<Workspace::Locator> locator
				= Workspace::Locator::Parse(message->GetString("locator", ""));
			if (locator.has_value()) {
				ShowSource(*locator, message->GetInt32("line", 0), message->GetInt32("session", 0));
			}
			break;
		}
		case Debugger::kMsgShowToolWindow:
			ShowToolWindow(message->GetInt32("session", 0),
				static_cast<Debugger::ToolWindowKind>(message->GetInt32("window", 0)));
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
		case kMsgShowOpenGit:
			(new OpenGitWindow(BMessenger(this), message->GetString("directory", "")))->Show();
			break;
		case kMsgShowOpenLocation:
			(new OpenLocationWindow(BMessenger(this)))->Show();
			break;
		case kMsgShowDocuments:
			ShowDocumentsWindow();
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
			if (fWindows.empty() && !fDocumentsWindow.IsValid()) {
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
