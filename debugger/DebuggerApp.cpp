#include <map>
#include <string>

#include <Application.h>
#include <Entry.h>
#include <Path.h>

#include "BreakpointsWindow.h"
#include "Common.h"
#include "ConsoleWindow.h"
#include "ControlWindow.h"
#include "DebugSession.h"
#include "Documents.h"
#include "SourceWindow.h"
#include "StackWindow.h"
#include "ThreadsWindow.h"
#include "VariablesWindow.h"


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditDebugger";


namespace Debugger {


static DebugSession *sSession = nullptr;


BMessenger SessionMessenger()
{
	return BMessenger(sSession);
}


class DebuggerApp: public BApplication {
private:
	std::string fProgram;
	std::string fArguments;
	std::map<std::string, SourceWindow*> fSourceWindows;
	std::map<ToolWindowKind, BWindow*> fToolWindows;

	BWindow *CreateToolWindow(ToolWindowKind kind);
	void ShowToolWindow(ToolWindowKind kind);
	void ShowSource(const std::string &path, int32 line);

public:
	DebuggerApp();

	void ArgvReceived(int32 argc, char **argv) override;
	void RefsReceived(BMessage *message) override;
	void ReadyToRun() override;
	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


DebuggerApp::DebuggerApp():
	BApplication(kAppSignature)
{
	sSession = new DebugSession();
	sSession->Run();
	DocumentRegistry::Default().SetListener(BMessenger(sSession));
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
		ShowSource(BPath(&ref).Path(), 0);
	}
}


void DebuggerApp::ReadyToRun()
{
	// The console exists from the start to collect all output.
	CreateToolWindow(ToolWindowKind::Console);
	ShowToolWindow(ToolWindowKind::Threads);
	ShowToolWindow(ToolWindowKind::Stack);
	ShowToolWindow(ToolWindowKind::Variables);
	(new ControlWindow(fProgram.c_str(), fArguments.c_str()))->Show();
}


BWindow *DebuggerApp::CreateToolWindow(ToolWindowKind kind)
{
	const auto it = fToolWindows.find(kind);
	if (it != fToolWindows.end()) {
		return it->second;
	}
	BWindow *window = nullptr;
	switch (kind) {
		case ToolWindowKind::Console:
			window = new ConsoleWindow();
			break;
		case ToolWindowKind::Threads:
			window = new ThreadsWindow();
			break;
		case ToolWindowKind::Stack:
			window = new StackWindow();
			break;
		case ToolWindowKind::Variables:
			window = new VariablesWindow();
			break;
		case ToolWindowKind::Breakpoints:
			window = new BreakpointsWindow();
			break;
		default:
			return nullptr;
	}
	// Run the window hidden, so it receives notices.
	window->Hide();
	window->Show();
	fToolWindows[kind] = window;
	return window;
}


void DebuggerApp::ShowToolWindow(ToolWindowKind kind)
{
	BWindow *window = CreateToolWindow(kind);
	if (window == nullptr || !window->Lock()) {
		return;
	}
	if (window->IsHidden()) {
		window->Show();
	}
	window->Activate();
	window->Unlock();
}


void DebuggerApp::ShowSource(const std::string &rawPath, int32 line)
{
	const BReference<SourceDocument> document = DocumentRegistry::Default().Open(rawPath);
	if (document == nullptr) {
		return;
	}
	SourceWindow *window;
	const auto it = fSourceWindows.find(document->path);
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
		window = new SourceWindow(document.Get());
		fSourceWindows[document->path] = window;
		window->Show();
	}
	if (line > 0) {
		window->RevealLine(line);
	}
}


void DebuggerApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgShowSource:
			ShowSource(message->GetString("path", ""), message->GetInt32("line", 0));
			break;
		case kMsgShowToolWindow:
			ShowToolWindow(static_cast<ToolWindowKind>(message->GetInt32("window", 0)));
			break;
		case kMsgSourceWindowClosed:
			fSourceWindows.erase(message->GetString("path", ""));
			break;
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
	if (sSession->Lock()) {
		sSession->Quit();
	}
	sSession = nullptr;
	return true;
}


} // Debugger


int main()
{
	Debugger::DebuggerApp app;
	app.Run();
	return 0;
}
