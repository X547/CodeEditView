#pragma once

#include <map>
#include <string>
#include <vector>

#include <Application.h>
#include <Locker.h>
#include <Messenger.h>

#include <CodeEditView/TextModel.h>

#include "Document.h"
#include "DocumentWatcher.h"
#include "Locator.h"
#include "Protocol.h"


class BFilePanel;

namespace Debugger {
class DebugSession;
}


namespace Studio {


struct SessionInfo {
	int32 id {};
	std::string name;
	BMessenger messenger;
};


// The CodeEdit application: documents shared by its windows, one window
// per view, and debug sessions that windows follow.
class StudioApp: public BApplication {
private:
	struct WindowEntry {
		BMessenger messenger;
		Workspace::Locator locator;
		// The debug session followed; 0 for none.
		int32 session {};
	};

	struct SessionEntry {
		Debugger::DebugSession *session {};
		std::string name;
		BWindow *control {};
		std::map<Debugger::ToolWindowKind, BWindow*> tools;
	};

	Workspace::DocumentRegistry fRegistry;
	Workspace::DocumentWatcher *fWatcher;
	BLocker fWindowLock;
	std::vector<WindowEntry> fWindows;
	BMessenger fDocumentsWindow;
	bool fStacking = true;
	BFilePanel *fOpenPanel {};
	std::map<int32, SessionEntry> fSessions;
	int32 fNextSessionId = 1;

	void ShowDocumentsWindow();
	void ShowOpenFilePanel();
	void OpenArgument(const std::string &argument, const char *cwd);
	void ShowSource(const Workspace::Locator &locator, int32 line, int32 session);
	void ShowToolWindow(int32 session, Debugger::ToolWindowKind kind);
	void CloseSession(int32 id);
	void QuitSessions();

public:
	StudioApp();
	~StudioApp() override;

	static StudioApp &Get();
	Workspace::DocumentRegistry &Registry() {return fRegistry;}

	// Activates a window showing the document of the locator, or opens one;
	// with newView always opens one. A new window follows the session and
	// reveals the line (1-based, 0 for none). It joins the stack of origin
	// (if stacking is on), which must be locked by the calling thread.
	void ShowDocument(const Workspace::Locator &locator, BWindow *origin, bool newView = false, int32 session = 0,
		int32 line = 0);
	// For source windows: their document or session changed, they close.
	void WindowShows(const BMessenger &window, const Workspace::Locator &locator, int32 session);
	void WindowClosing(const BMessenger &window);

	// Debug sessions. NewSession() shows the session's control window and
	// returns its id.
	int32 NewSession();
	std::vector<SessionInfo> Sessions();
	// The marks of a session in a model; none if the session is gone. Must
	// not be called with the model locked.
	BReference<CodeEdit::MarkSet> SessionMarks(int32 session, CodeEdit::TextModel *model);
	void DocumentsWindowClosing();
	int32 CountWindowsShowing(const Workspace::Locator &locator);

	bool Stacking();
	void SetStacking(bool stacking);

	void ArgvReceived(int32 argc, char **argv) override;
	void RefsReceived(BMessage *message) override;
	void ReadyToRun() override;
	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Studio
