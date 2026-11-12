#include "Common.h"

#include <Application.h>
#include <ColumnListView.h>
#include <Menu.h>
#include <MenuItem.h>

#include "BreakpointsWindow.h"
#include "ConsoleWindow.h"
#include "StackWindow.h"
#include "ThreadsWindow.h"
#include "VariablesWindow.h"


namespace Debugger {


bool gQuitting = false;


static BMenuItem *SessionItem(const BMessenger &session, const char *label, uint32 what, char shortcut = 0)
{
	BMenuItem *item = new BMenuItem(label, new BMessage(what), shortcut);
	item->SetTarget(session);
	return item;
}


BMenu *CreateDebugMenu(const BMessenger &session)
{
	BMenu *menu = new BMenu("Debug");
	menu->AddItem(SessionItem(session, "Continue", kMsgContinue, 'R'));
	menu->AddItem(SessionItem(session, "Pause", kMsgPause, 'P'));
	menu->AddSeparatorItem();
	menu->AddItem(SessionItem(session, "Step over", kMsgStepOver, 'N'));
	menu->AddItem(SessionItem(session, "Step into", kMsgStepInto, 'I'));
	menu->AddItem(SessionItem(session, "Step out", kMsgStepOut, 'T'));
	menu->AddSeparatorItem();
	menu->AddItem(SessionItem(session, "Stop", kMsgStop));
	return menu;
}


BWindow *CreateToolWindow(ToolWindowKind kind, const SessionContext &context)
{
	BWindow *window = nullptr;
	switch (kind) {
		case ToolWindowKind::Console:
			window = new ConsoleWindow(context);
			break;
		case ToolWindowKind::Threads:
			window = new ThreadsWindow(context);
			break;
		case ToolWindowKind::Stack:
			window = new StackWindow(context);
			break;
		case ToolWindowKind::Variables:
			window = new VariablesWindow(context);
			break;
		case ToolWindowKind::Breakpoints:
			window = new BreakpointsWindow(context);
			break;
		default:
			return nullptr;
	}
	window->Hide();
	window->Show();
	return window;
}


BMenu *CreateWindowsMenu(int32 sessionId)
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
		message->AddInt32("session", sessionId);
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
	for (const CodeEdit::MarkInfo &mark : model.SharedMarks().Marks(line, line)) {
		if (mark.kind == CodeEdit::kMarkBreakpoint || mark.kind == CodeEdit::kMarkBreakpointDisabled) {
			model.SharedMarks().RemoveMark(mark.id);
			removed = true;
		}
	}
	if (!removed) {
		model.SharedMarks().AddMark(CodeEdit::kMarkBreakpoint, line, 0, CodeEdit::MarkDeletePolicy::MoveToEditLine);
	}
}


void ToggleBreakpointEnabled(CodeEdit::TextModel &model, size_t line)
{
	CodeEdit::ModelLock lock(model);
	for (const CodeEdit::MarkInfo &mark : model.SharedMarks().Marks(line, line)) {
		if (mark.kind == CodeEdit::kMarkBreakpoint) {
			model.SharedMarks().SetMarkKind(mark.id, CodeEdit::kMarkBreakpointDisabled);
		} else if (mark.kind == CodeEdit::kMarkBreakpointDisabled) {
			model.SharedMarks().SetMarkKind(mark.id, CodeEdit::kMarkBreakpoint);
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

ToolWindow::ToolWindow(BRect frame, const char *title, const SessionContext &context):
	BWindow(frame, (title + context.titleSuffix).c_str(), B_FLOATING_WINDOW_LOOK, B_FLOATING_APP_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fBaseTitle(title),
	fContext(context)
{
	Watch(kMsgWatch);
}


void ToolWindow::Watch(uint32 what)
{
	if (!Session().IsValid()) {
		return;
	}
	BMessage watch(what);
	watch.AddMessenger("target", BMessenger(this));
	Session().SendMessage(&watch);
}


void ToolWindow::SetContext(const SessionContext &context)
{
	if (context.session == fContext.session && context.id == fContext.id) {
		return;
	}
	Watch(kMsgUnwatch);
	fContext = context;
	SetTitle((fBaseTitle + fContext.titleSuffix).c_str());
	SessionChanged();
	Watch(kMsgWatch);
}


void ToolWindow::DispatchMessage(BMessage *message, BHandler *handler)
{
	switch (message->what) {
		case kNoticeState:
		case kNoticeOutput:
		case kNoticeThreads:
		case kNoticeStack:
		case kNoticeScopes:
		case kNoticeVariables:
		case kNoticeBreakpoints:
			if (message->GetInt32("session", fContext.id) != fContext.id || !Session().IsValid()) {
				return;
			}
			break;
		default:
			break;
	}
	BWindow::DispatchMessage(message, handler);
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
