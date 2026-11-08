#include <map>
#include <string>

#include <Application.h>
#include <Entry.h>
#include <Path.h>

#include "Common.h"
#include "ControlWindow.h"
#include "DebugSession.h"
#include "Document.h"
#include "SourceWindow.h"


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditDebugger";


namespace Debugger {


static constexpr int32 kSessionId = 1;


class DebuggerApp: public BApplication {
private:
	Workspace::DocumentRegistry fRegistry;
	DebugSession *fSession;
	SessionContext fContext;
	std::string fProgram;
	std::string fArguments;
	std::map<Workspace::Locator, SourceWindow*> fSourceWindows;
	std::map<ToolWindowKind, BWindow*> fToolWindows;

	BWindow *ToolWindow(ToolWindowKind kind);
	void ShowToolWindow(ToolWindowKind kind);
	void ShowSource(const Workspace::Locator &locator, int32 line);

public:
	DebuggerApp();

	void ArgvReceived(int32 argc, char **argv) override;
	void RefsReceived(BMessage *message) override;
	void ReadyToRun() override;
	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


DebuggerApp::DebuggerApp():
	BApplication(kAppSignature),
	fSession(new DebugSession(fRegistry, kSessionId))
{
	fSession->Run();
	fContext.session = BMessenger(fSession);
	fContext.id = kSessionId;
	fContext.registry = &fRegistry;
}


// CodeEditDebugger [program [arguments...]]
void DebuggerApp::ArgvReceived(int32 argc, char **argv)
{
	if (argc < 2) {
		return;
	}
	const char *cwd = CurrentMessage() != nullptr ? CurrentMessage()->GetString("cwd", nullptr) : nullptr;
	BPath program;
	if (argv[1][0] == '/' || cwd == nullptr) {
		program.SetTo(argv[1]);
	} else {
		program.SetTo(cwd, argv[1]);
	}
	fProgram = program.Path() != nullptr ? program.Path() : argv[1];
	fArguments.clear();
	for (int32 i = 2; i < argc; i++) {
		if (i > 2) {
			fArguments += ' ';
		}
		fArguments += argv[i];
	}
}


void DebuggerApp::RefsReceived(BMessage *message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		ShowSource(Workspace::Locator::File(BPath(&ref).Path()), 0);
	}
}


void DebuggerApp::ReadyToRun()
{
	// The console exists from the start to collect all output.
	ToolWindow(ToolWindowKind::Console);
	ShowToolWindow(ToolWindowKind::Threads);
	ShowToolWindow(ToolWindowKind::Stack);
	ShowToolWindow(ToolWindowKind::Variables);
	(new ControlWindow(fContext, "CodeEditDebugger", fProgram.c_str(), fArguments.c_str(),
		BMessage(B_QUIT_REQUESTED)))->Show();
}


BWindow *DebuggerApp::ToolWindow(ToolWindowKind kind)
{
	BWindow *&window = fToolWindows[kind];
	if (window == nullptr) {
		window = CreateToolWindow(kind, fContext);
	}
	return window;
}


void DebuggerApp::ShowToolWindow(ToolWindowKind kind)
{
	BWindow *window = ToolWindow(kind);
	if (window == nullptr || !window->Lock()) {
		return;
	}
	if (window->IsHidden()) {
		window->Show();
	}
	window->Activate();
	window->Unlock();
}


void DebuggerApp::ShowSource(const Workspace::Locator &requested, int32 line)
{
	const BReference<Workspace::Document> document = fRegistry.Open(requested);
	const Workspace::Locator &locator = document->GetLocator();
	SourceWindow *window;
	const auto it = fSourceWindows.find(locator);
	if (it != fSourceWindows.end()) {
		window = it->second;
		if (window->Lock()) {
			if (window->IsHidden()) {
				window->Show();
			}
			window->Activate();
			window->Unlock();
		}
	} else {
		window = new SourceWindow(document, fContext, fSession);
		fSourceWindows[locator] = window;
		window->Show();
	}
	if (line > 0) {
		window->RevealLine(line);
	}
}


void DebuggerApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgShowSource: {
			const std::optional<Workspace::Locator> locator = Workspace::Locator::Parse(message->GetString("locator", ""));
			if (locator.has_value()) {
				ShowSource(*locator, message->GetInt32("line", 0));
			}
			break;
		}
		case kMsgShowToolWindow:
			ShowToolWindow(static_cast<ToolWindowKind>(message->GetInt32("window", 0)));
			break;
		case kMsgSourceWindowClosed: {
			const std::optional<Workspace::Locator> locator = Workspace::Locator::Parse(message->GetString("locator", ""));
			if (locator.has_value()) {
				fSourceWindows.erase(*locator);
			}
			break;
		}
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


bool DebuggerApp::QuitRequested()
{
	gQuitting = true;
	if (!BApplication::QuitRequested()) {
		gQuitting = false;
		return false;
	}
	if (fSession->Lock()) {
		fSession->Quit();
	}
	fSession = nullptr;
	return true;
}


} // Debugger


int main()
{
	Debugger::DebuggerApp app;
	app.Run();
	return 0;
}
