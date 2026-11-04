#include "Common.h"

#include <Application.h>
#include <ColumnListView.h>
#include <Menu.h>
#include <MenuItem.h>


namespace Debugger {


bool gQuitting = false;


static BMenuItem *SessionItem(const char *label, uint32 what, char shortcut = 0, uint32 modifiers = 0)
{
	BMenuItem *item = new BMenuItem(label, new BMessage(what), shortcut, modifiers);
	item->SetTarget(SessionMessenger());
	return item;
}


BMenu *CreateDebugMenu()
{
	BMenu *menu = new BMenu("Debug");
	menu->AddItem(SessionItem("Continue", kMsgContinue, 'R'));
	menu->AddItem(SessionItem("Pause", kMsgPause, 'P'));
	menu->AddSeparatorItem();
	menu->AddItem(SessionItem("Step over", kMsgStepOver, 'N'));
	menu->AddItem(SessionItem("Step into", kMsgStepInto, 'I'));
	menu->AddItem(SessionItem("Step out", kMsgStepOut, 'T'));
	menu->AddSeparatorItem();
	menu->AddItem(SessionItem("Stop", kMsgStop));
	return menu;
}


BMenu *CreateWindowsMenu()
{
	struct Entry {
		const char *label;
		ToolWindowKind window;
		char shortcut;
	};
	static constexpr Entry kEntries[] = {
		{"Console", ToolWindowKind::Console, '1'},
		{"Threads", ToolWindowKind::Threads, '2'},
		{"Stack", ToolWindowKind::Stack, '3'},
		{"Variables", ToolWindowKind::Variables, '4'},
		{"Breakpoints", ToolWindowKind::Breakpoints, '5'},
	};
	BMenu *menu = new BMenu("Windows");
	for (const Entry &entry : kEntries) {
		BMessage *message = new BMessage(kMsgShowToolWindow);
		message->AddInt32("window", static_cast<int32>(entry.window));
		BMenuItem *item = new BMenuItem(entry.label, message, entry.shortcut);
		item->SetTarget(be_app_messenger);
		menu->AddItem(item);
	}
	return menu;
}


void ToggleBreakpoint(CodeEdit::TextModel &model, size_t line)
{
	CodeEdit::ModelLock lock(model);
	bool removed = false;
	for (const CodeEdit::MarkInfo &mark : model.Marks(line, line)) {
		if (mark.kind == CodeEdit::kMarkBreakpoint || mark.kind == CodeEdit::kMarkBreakpointDisabled) {
			model.RemoveMark(mark.id);
			removed = true;
		}
	}
	if (!removed) {
		model.AddMark(CodeEdit::kMarkBreakpoint, line, 0, CodeEdit::MarkDeletePolicy::MoveToEditLine);
	}
}


void ToggleBreakpointEnabled(CodeEdit::TextModel &model, size_t line)
{
	CodeEdit::ModelLock lock(model);
	for (const CodeEdit::MarkInfo &mark : model.Marks(line, line)) {
		if (mark.kind == CodeEdit::kMarkBreakpoint) {
			model.SetMarkKind(mark.id, CodeEdit::kMarkBreakpointDisabled);
		} else if (mark.kind == CodeEdit::kMarkBreakpointDisabled) {
			model.SetMarkKind(mark.id, CodeEdit::kMarkBreakpoint);
		}
	}
}


BColumnListView *CreateColumnList(const char *name, uint32 selectionWhat, uint32 invocationWhat)
{
	BColumnListView *list = new BColumnListView(name, B_NAVIGABLE, B_NO_BORDER, false);
	list->SetSortingEnabled(false);
	list->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	list->SetSelectionMessage(new BMessage(selectionWhat));
	list->SetInvocationMessage(new BMessage(invocationWhat));
	return list;
}


std::string FileName(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


//#pragma mark - ToolWindow

ToolWindow::ToolWindow(BRect frame, const char *title):
	BWindow(frame, title, B_FLOATING_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS)
{
	BMessage watch(kMsgWatch);
	watch.AddMessenger("target", BMessenger(this));
	SessionMessenger().SendMessage(&watch);
}


bool ToolWindow::QuitRequested()
{
	if (gQuitting) {
		return true;
	}
	if (!IsHidden()) {
		Hide();
	}
	return false;
}


} // Debugger
