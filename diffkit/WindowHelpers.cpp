#include "WindowHelpers.h"

#include <Button.h>
#include <FilePanel.h>
#include <MessageFilter.h>
#include <TextControl.h>
#include <TextView.h>
#include <Window.h>

#include <CodeEditView/CodeEditView.h>


namespace DiffKit {


void PlaceOver(BWindow *window, BRect openerFrame)
{
	window->CenterIn(openerFrame);
	window->MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
}


void PlacePanel(BFilePanel *panel, BRect openerFrame)
{
	BWindow *window = panel->Window();
	if (window == nullptr || !window->Lock()) {
		return;
	}
	if (window->IsHidden()) {
		PlaceOver(window, openerFrame);
	}
	window->Unlock();
}


// BWindow makes the default button the target of Enter before the filters
// run; this filter gives the key back to an editable focused editor.
class EnterToEditorFilter: public BMessageFilter {
public:
	EnterToEditorFilter():
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE)
	{
	}

	filter_result Filter(BMessage *message, BHandler **target) final
	{
		if (message->what != B_KEY_DOWN && message->what != B_KEY_UP) {
			return B_DISPATCH_MESSAGE;
		}
		BWindow *window = dynamic_cast<BWindow*>(Looper());
		if (window == nullptr || window->DefaultButton() == nullptr || *target != window->DefaultButton()) {
			return B_DISPATCH_MESSAGE;
		}
		BView *focus = window->CurrentFocus();
		if (IsMultiLineEditor(focus)) {
			*target = focus;
		}
		return B_DISPATCH_MESSAGE;
	}

private:
	// The text view of a text control is a single line: Enter there is for
	// the button.
	static bool IsMultiLineEditor(BView *view)
	{
		if (CodeEditView *editor = dynamic_cast<CodeEditView*>(view)) {
			return !editor->Settings().readOnly;
		}
		BTextView *textView = dynamic_cast<BTextView*>(view);
		return textView != nullptr && textView->IsEditable()
			&& dynamic_cast<BTextControl*>(textView->Parent()) == nullptr;
	}
};


void KeepEnterInEditors(BWindow *window)
{
	window->AddCommonFilter(new EnterToEditorFilter());
}


} // DiffKit
