#pragma once

#include <Referenceable.h>
#include <Window.h>

#include <CodeEditView/TextModel.h>

#include "Document.h"


class BFilePanel;
class BMenu;
class BMenuItem;
class BSplitView;
class BStringView;
class CodeEditView;
class OutlineView;


namespace Studio {


// A window with one view of a document.
class SourceWindow: public BWindow {
private:
	BReference<Workspace::Document> fDocument;
	CodeEditView *fEditView;
	BStringView *fStatus;
	BSplitView *fSplitView {};
	OutlineView *fOutline {};
	BMenuItem *fOutlineItem {};
	bool fOutlineRefreshPending {};
	// The debug session and coverage run followed; 0 for none.
	int32 fSession {};
	int32 fCoverage {};
	BReference<CodeEdit::DecorationSet> fCoverageSet;
	BMenu *fDebugMenu {};
	BMenu *fSessionMenu {};
	BMenu *fCoverageMenu {};
	BMenu *fCoverageRunMenu {};
	BFilePanel *fOpenPanel {};
	BFilePanel *fSavePanel {};
	// Close when the save started by QuitRequested() finishes.
	bool fCloseAfterSave {};

	bool IsDocumentNotice(BMessage *message) const;
	void UpdateTitle();
	void UpdateStatus();
	void Save();
	void ShowSavePanel();
	void SaveAs(BMessage *message);
	void Reload();
	void SavedNotice(BMessage *message);
	void ShowOpenPanel();
	void OpenRefs(BMessage *message);
	std::string StartDirectory() const;
	void ScheduleOutlineRefresh();
	void RefreshOutline();
	void JumpToOutlineItem(bool focus);
	void ToggleOutline();
	BMenu *CreateDebugMenu();
	void UpdateDebugMenu();
	BMenu *CreateCoverageMenu();
	void UpdateCoverageMenu();
	void Follow(int32 session, int32 coverage);
	void GoToUncovered(bool forward);
	BMessenger SessionMessenger() const;
	void Reveal(int32 line);

public:
	SourceWindow(Workspace::Document *document, BRect frame, int32 session, int32 coverage);
	~SourceWindow() override;

	void MessageReceived(BMessage *message) override;
	void MenusBeginning() override;
	void WindowActivated(bool active) override;
	bool QuitRequested() override;
};


} // Studio
