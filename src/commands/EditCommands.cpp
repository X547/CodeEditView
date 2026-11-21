#include "EditCommands.h"

#include <string>

#include <AppDefs.h>
#include <Looper.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>

#include <CodeEditView/CodeEditView.h>


namespace CodeEdit {


namespace EditorCommands {


// Executed by the view itself.
class EditCommandSet: public EditorCommandSet {
private:
	static bool HasSelection(CodeEditView &view);
	static bool IsEditable(CodeEditView &view) {return !view.Settings().readOnly;}

public:
	bool Handles(uint32 what) const final;
	bool IsEnabled(uint32 what, CodeEditView *view) final;
	void Execute(BMessage *message, CodeEditView *view) final;
	void UpdateItem(BMenuItem &item, CodeEditView *view) final;
};


bool EditCommandSet::HasSelection(CodeEditView &view)
{
	const Selection selection = view.GetSelection();
	return selection.anchor != selection.caret;
}


bool EditCommandSet::Handles(uint32 what) const
{
	switch (what) {
		case B_UNDO:
		case B_REDO:
		case B_CUT:
		case B_COPY:
		case B_PASTE:
		case B_SELECT_ALL:
			return true;
		default:
			return false;
	}
}


bool EditCommandSet::IsEnabled(uint32 what, CodeEditView *view)
{
	if (view == nullptr) {
		return false;
	}
	switch (what) {
		case B_UNDO:
		case B_REDO: {
			if (!IsEditable(*view)) {
				return false;
			}
			TextModel &model = *view->Model();
			ModelLock lock(model);
			return what == B_UNDO ? model.CanUndo() : model.CanRedo();
		}
		case B_CUT:
			return IsEditable(*view) && HasSelection(*view);
		case B_COPY:
			return HasSelection(*view);
		case B_PASTE:
			return IsEditable(*view);
		case B_SELECT_ALL:
			return true;
		default:
			return false;
	}
}


void EditCommandSet::Execute(BMessage *message, CodeEditView *view)
{
	if (view == nullptr || view->Looper() == nullptr) {
		return;
	}
	view->Looper()->PostMessage(message, view);
}


void EditCommandSet::UpdateItem(BMenuItem &item, CodeEditView *view)
{
	const uint32 what = item.Message()->what;
	item.SetEnabled(IsEnabled(what, view));
	if (what != B_UNDO && what != B_REDO) {
		return;
	}
	std::string label = what == B_UNDO ? "Undo" : "Redo";
	if (view != nullptr) {
		TextModel &model = *view->Model();
		ModelLock lock(model);
		const std::string name = what == B_UNDO ? model.UndoName() : model.RedoName();
		if (!name.empty()) {
			label += " " + name;
		}
	}
	item.SetLabel(label.c_str());
}


EditorCommandSet &BuiltInEditCommands()
{
	static EditCommandSet sCommands;
	return sCommands;
}


void AddEditItems(BMenu *menu)
{
	menu->AddItem(new BMenuItem("Undo", new BMessage(B_UNDO), 'Z'));
	menu->AddItem(new BMenuItem("Redo", new BMessage(B_REDO), 'Z', B_SHIFT_KEY));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Cut", new BMessage(B_CUT), 'X'));
	menu->AddItem(new BMenuItem("Copy", new BMessage(B_COPY), 'C'));
	menu->AddItem(new BMenuItem("Paste", new BMessage(B_PASTE), 'V'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Select all", new BMessage(B_SELECT_ALL), 'A'));
}


} // EditorCommands


} // CodeEdit
