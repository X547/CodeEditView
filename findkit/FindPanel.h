#pragma once

#include <Window.h>


class BCheckBox;
class BStringView;
class CodeEditView;


namespace FindKit {


// The application's find and replace panel. A tool window: its buttons and
// find shortcuts are forwarded to the active host window
// (CodeEdit::EditorCommands::ActiveHost()), never executed on its own
// fields. Closing it only hides it.
class FindPanel: public BWindow {
private:
	CodeEditView *fFindField {};
	CodeEditView *fReplaceField {};
	BCheckBox *fRegexBox {};
	BCheckBox *fIgnoreCaseBox {};
	BCheckBox *fWrapBox {};
	BCheckBox *fBackwardBox {};
	BCheckBox *fInSelectionBox {};
	BStringView *fStatus {};

	FindPanel();

	CodeEditView *CreateField(const char *name);
	void LoadState();
	void StoreState();
	void Forward(uint32 what);
	void FocusFindField();
	void SetStatus(const char *status, bool error);

public:
	// Shows and activates the panel, creating it on first use.
	static void ShowPanel();

	bool QuitRequested() final;
	void DispatchMessage(BMessage *message, BHandler *target) final;
	void MessageReceived(BMessage *message) final;
	void WindowActivated(bool active) final;
};


} // FindKit
