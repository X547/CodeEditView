#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <Application.h>
#include <Locker.h>
#include <Messenger.h>
#include <Rect.h>

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


class CoverageRun;


struct SessionInfo {
	int32 id {};
	std::string name;
	BMessenger messenger;
};


struct CoverageInfo {
	int32 id {};
	std::string name;
};


// How StudioApp::ShowDocument() shows a document.
struct ShowOptions {
	// Open another window even if one shows the document.
	bool newView {};
	// The debug session and coverage run a new window follows; 0 for none.
	int32 session {};
	int32 coverage {};
	// 1-based; 0 for none.
	int32 line {};
};


// The CodeEdit application: documents shared by its windows, one window
// per view, and the debug sessions and coverage runs that windows follow.
class StudioApp: public BApplication {
private:
	struct WindowEntry {
		BMessenger messenger;
		Workspace::Locator locator;
		// The debug session and coverage run followed; 0 for none.
		int32 session {};
		int32 coverage {};
	};

	struct SessionEntry {
		Debugger::DebugSession *session {};
		std::string name;
		BWindow *control {};
		std::map<Debugger::ToolWindowKind, BWindow*> tools;
	};

	struct CoverageEntry {
		CoverageRun *run {};
		BWindow *summary {};
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
	std::map<int32, CoverageEntry> fCoverages;
	int32 fNextCoverageId = 1;

	// Windows opened from another window are placed over its frame.
	void ShowDocumentsWindow(std::optional<BRect> openerFrame = std::nullopt);
	void ShowOpenFilePanel();
	void OpenArgument(const std::string &argument, const char *cwd);
	void ShowFollowing(const Workspace::Locator &locator, const ShowOptions &options);
	void ShowToolWindow(int32 session, Debugger::ToolWindowKind kind, std::optional<BRect> openerFrame);
	void CloseSession(int32 id);
	void OpenCoverage(const std::string &binary, const std::string &profile);
	void CloseCoverage(int32 id);
	void QuitSessions();
	void BroadcastToSourceWindows(BMessage &message);

public:
	StudioApp();
	~StudioApp() override;

	static StudioApp &Get();
	Workspace::DocumentRegistry &Registry() {return fRegistry;}

	// Activates a window showing the document of the locator, or opens one
	// (see ShowOptions). A new window joins the stack of origin (if stacking
	// is on), which must be locked by the calling thread.
	void ShowDocument(const Workspace::Locator &locator, BWindow *origin, const ShowOptions &options = {});
	// Shows a new untitled document in a new window (see ShowDocument()).
	void NewDocument(BWindow *origin);
	// The file chosen in a save panel ("directory", "name"); none if the
	// message has none.
	static std::optional<Workspace::Locator> ChosenLocator(const BMessage &message);
	// For source windows: their document, session or coverage run changed,
	// they close.
	void WindowShows(const BMessenger &window, const Workspace::Locator &locator, int32 session, int32 coverage);
	void WindowClosing(const BMessenger &window);

	// Debug sessions. NewSession() shows the session's control window over
	// the window that asks for it and returns its id.
	int32 NewSession(BRect openerFrame);
	std::vector<SessionInfo> Sessions();
	// The marks of a session in a model; none if the session is gone. Must
	// not be called with the model locked.
	BReference<CodeEdit::MarkSet> SessionMarks(int32 session, CodeEdit::TextModel *model);

	// Coverage runs, opened with kMsgOpenCoverage ("binary", "profile").
	std::vector<CoverageInfo> Coverages();
	// The coverage of a document by a run; none if the run is gone or has no
	// path for the document. Must not be called with the model locked.
	BReference<CodeEdit::DecorationSet> CoverageDecorations(int32 coverage, const Workspace::Locator &locator,
		CodeEdit::TextModel *model);
	// For a status line; empty if the run is gone.
	std::string CoverageStatus(int32 coverage, const Workspace::Locator &locator);
	// Watchers get kNoticeCoverageChanged.
	void WatchCoverage(int32 coverage, const BMessenger &watcher, bool watch);
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
