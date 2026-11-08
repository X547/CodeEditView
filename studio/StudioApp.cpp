#include "StudioApp.h"

#include <algorithm>

#include <Alert.h>
#include <Autolock.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Path.h>
#include <WindowStack.h>

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
	fWatcher->Lock();
	fWatcher->Quit();
	delete fOpenPanel;
}


StudioApp &StudioApp::Get()
{
	return *static_cast<StudioApp*>(be_app);
}


void StudioApp::ShowDocument(const Workspace::Locator &locator, BWindow *origin, bool newView)
{
	BReference<Workspace::Document> document = fRegistry.Open(locator);
	fWatcher->Update();
	if (!newView) {
		BAutolock lock(fWindowLock);
		for (const WindowEntry &entry : fWindows) {
			if (entry.locator == document->GetLocator()) {
				entry.messenger.SendMessage(kMsgActivate);
				return;
			}
		}
	}

	const bool stack = origin != nullptr && Stacking();
	SourceWindow *window = new SourceWindow(document, stack ? origin->Frame() : NextWindowFrame());
	WindowShows(BMessenger(window), document->GetLocator());
	window->Show();
	if (stack) {
		BWindowStack(origin).AddWindow(BMessenger(window));
	}
}


void StudioApp::WindowShows(const BMessenger &window, const Workspace::Locator &locator)
{
	{
		BAutolock lock(fWindowLock);
		const auto it = std::find_if(fWindows.begin(), fWindows.end(), [&](const WindowEntry &entry) {
			return entry.messenger == window;
		});
		if (it != fWindows.end()) {
			it->locator = locator;
		} else {
			fWindows.push_back({window, locator});
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
				ShowDocument(*locator, nullptr);
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


} // Studio


int main()
{
	Studio::StudioApp app;
	app.Run();
	return 0;
}
