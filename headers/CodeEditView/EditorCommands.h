#pragma once

#include <Messenger.h>
#include <SupportDefs.h>


class BMenu;
class BMenuItem;
class BMessage;
class BWindow;
class CodeEditView;


namespace CodeEdit {


// Commands that act on the focused code editor view of a window and on its
// model (e.g. find and replace), routed by menus and tool windows.
//
// Adding editor commands to an app, in each host window (a window with code
// editor views):
//
//   WindowActivated(active):
//     if (active) EditorCommands::SetActiveHost(this);
//   MenusBeginning():
//     EditorCommands::UpdateMenu(menuBar, EditorCommands::FocusedEditor(this));
//   MessageReceived(message):
//     if (EditorCommands::Dispatch(message, EditorCommands::FocusedEditor(this))) return;
//
// plus the menu items of each command set (e.g. EditorCommands::AddEditItems(),
// FindKit::AddFindItems()). A host with several editors may pass its own
// choice of view (e.g. the last focused one) instead of FocusedEditor().
//
// Tool windows (panels) never execute commands and never become the active
// host: they post commands to ActiveHost(), so the commands act on the
// document even if the tool window itself contains code editor views.
class EditorCommandSet {
public:
	virtual ~EditorCommandSet() = default;

	virtual bool Handles(uint32 what) const = 0;
	// view is nullptr if there is no target editor.
	virtual bool IsEnabled(uint32 what, CodeEditView *view) = 0;
	virtual void Execute(BMessage *message, CodeEditView *view) = 0;
	// Updates a menu item of a command of the set before the menu is shown;
	// enables it as IsEnabled() says unless overridden (e.g. to change the
	// label).
	virtual void UpdateItem(BMenuItem &item, CodeEditView *view);
};


namespace EditorCommands {


// Process-wide registry of command sets; they are not owned.
void Register(EditorCommandSet *set);
void Unregister(EditorCommandSet *set);

// The window's focus view if it is a code editor view, else nullptr.
CodeEditView *FocusedEditor(BWindow *window);

// The last activated host window; invalid if it is gone.
void SetActiveHost(BWindow *window);
BMessenger ActiveHost();

// Enables the items of the menu and its submenus whose commands belong to a
// registered set.
void UpdateMenu(BMenu *menu, CodeEditView *view);
// Executes the message if a registered set handles it.
bool Dispatch(BMessage *message, CodeEditView *view);

// Adds the undo, redo, clipboard and select all items to the menu and
// registers their commands (B_UNDO, B_REDO, B_CUT, B_COPY, B_PASTE,
// B_SELECT_ALL). Undo and redo are labeled with the name of the undo step
// (UndoScript) and disabled without one.
void AddEditItems(BMenu *menu);


} // EditorCommands


} // CodeEdit
