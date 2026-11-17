#pragma once

#include <Referenceable.h>
#include <Window.h>

#include <CodeEditView/TextModel.h>

#include "Document.h"


class BFilePanel;
class BMenu;
class BMenuBar;
class BMenuItem;
class BSplitView;
class BStringView;
class CodeEditView;
class OutlineView;


namespace Studio {


class DefinitionsMenu;
class IconMenuItem;
class IncludesMenu;
class OpenMenu;
struct ShowOptions;


// A window with one view of a document.
class SourceWindow: public BWindow {
private:
	BReference<Workspace::Document> fDocument;
	CodeEditView *fEditView;
	BStringView *fStatus;
	BSplitView *fSplitView {};
	OutlineView *fOutline {};
	BMenuItem *fOutlineItem {};
	BMenuItem *fStickyItem {};
	bool fOutlineRefreshPending {};
	// The debug session and coverage run followed; 0 for none.
	int32 fSession {};
	int32 fCoverage {};
	BReference<CodeEdit::DecorationSet> fCoverageSet;
	BMenu *fDebugMenu {};
	BMenu *fSessionMenu {};
	BMenu *fCoverageMenu {};
	BMenu *fCoverageRunMenu {};
	// Of the tool bar: the drop-down menus, made when shown, and the
	// buttons with a state.
	OpenMenu *fOpenMenu {};
	DefinitionsMenu *fFunctionsMenu {};
	IncludesMenu *fHeadersMenu {};
	IconMenuItem *fSaveButton {};
	IconMenuItem *fLockButton {};
	IconMenuItem *fOutlineButton {};
	// Editing is turned off from the tool bar.
	bool fLocked {};
	BFilePanel *fOpenPanel {};
	BFilePanel *fSavePanel {};
	// Close when the save started by QuitRequested() finishes.
	// Closing asks about unsaved changes without blocking.
	bool fAskingClose {};
	bool fCloseAfterSave {};
	// A file was chosen in the save panel since it was shown for save as.
	bool fSaveAsChosen {};

	bool IsDocumentNotice(BMessage *message) const;
	void UpdateTitle();
	void UpdateStatus();
	void Save();
	void ShowSavePanel(uint32 chosen, const char *title, const std::string &name);
	void SavePanelClosed(BMessage *message);
	void SetDocument(Workspace::Document *document);
	void SaveAs(BMessage *message);
	void DocumentMoved(BMessage *message);
	void SaveCopy(BMessage *message);
	void CopySavedNotice(BMessage *message);
	void Revert();
	void SavedNotice(BMessage *message);
	void ShowOpenPanel();
	void OpenRefs(BMessage *message);
	void PositionHeld(BMessage *message);
	std::string StartDirectory() const;
	void ScheduleOutlineRefresh();
	void RefreshOutline();
	void JumpToOutlineItem(bool focus);
	void ToggleOutline();
	BMenu *CreateDebugMenu();
	void UpdateDebugMenu();
	BMenu *CreateCoverageMenu();
	void UpdateCoverageMenu();
	BMenuBar *CreateToolBar();
	void UpdateToolBar();
	void OpenInclude(BMessage *message);
	void ToggleLocked();
	void Follow(int32 session, int32 coverage);
	void GoToUncovered(bool forward);
	BMessenger SessionMessenger() const;
	void Reveal(const ShowOptions &options);
	void ShowSessionInTools();

public:
	SourceWindow(Workspace::Document *document, BRect frame, int32 session, int32 coverage);
	~SourceWindow() override;

	void MessageReceived(BMessage *message) override;
	void MenusBeginning() override;
	void WindowActivated(bool active) override;
	bool QuitRequested() override;
};


} // Studio
