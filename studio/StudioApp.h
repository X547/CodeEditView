#pragma once

#include <string>
#include <vector>

#include <Application.h>
#include <Locker.h>
#include <Messenger.h>

#include "Document.h"
#include "DocumentWatcher.h"
#include "Locator.h"


class BFilePanel;


namespace Studio {


// The CodeEdit application: documents shared by its windows, one window
// per view.
class StudioApp: public BApplication {
private:
	struct WindowEntry {
		BMessenger messenger;
		Workspace::Locator locator;
	};

	Workspace::DocumentRegistry fRegistry;
	Workspace::DocumentWatcher *fWatcher;
	BLocker fWindowLock;
	std::vector<WindowEntry> fWindows;
	BMessenger fDocumentsWindow;
	bool fStacking = true;
	BFilePanel *fOpenPanel {};

	void ShowDocumentsWindow();
	void ShowOpenFilePanel();
	void OpenArgument(const std::string &argument, const char *cwd);

public:
	StudioApp();
	~StudioApp() override;

	static StudioApp &Get();
	Workspace::DocumentRegistry &Registry() {return fRegistry;}

	// Activates a window showing the document of the locator, or opens one;
	// with newView always opens one. A new window joins the stack of origin
	// (if stacking is on), which must be locked by the calling thread.
	void ShowDocument(const Workspace::Locator &locator, BWindow *origin, bool newView = false);
	// For source windows: their document changed (save as), they close.
	void WindowShows(const BMessenger &window, const Workspace::Locator &locator);
	void WindowClosing(const BMessenger &window);
	void DocumentsWindowClosing();
	int32 CountWindowsShowing(const Workspace::Locator &locator);

	bool Stacking();
	void SetStacking(bool stacking);

	void ArgvReceived(int32 argc, char **argv) override;
	void RefsReceived(BMessage *message) override;
	void ReadyToRun() override;
	void MessageReceived(BMessage *message) override;
};


} // Studio
