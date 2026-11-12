#pragma once

#include <stddef.h>

#include <string>

#include <Messenger.h>
#include <Window.h>

#include <CodeEditView/TextModel.h>

#include "Protocol.h"


class BColumnListView;
class BMenu;

namespace Workspace {
class DocumentRegistry;
}


namespace Debugger {


// What the windows of a session need.
struct SessionContext {
	BMessenger session;
	// Sent with requests to the application (kMsgShowSource,
	// kMsgShowToolWindow).
	int32 id {};
	// Appended to window titles, e.g. " — Session 2".
	std::string titleSuffix;
	Workspace::DocumentRegistry *registry {};
};


// Set while the application quits, so tool windows close instead of hiding.
extern bool gQuitting;

// Menus shared by the windows: run control (targeting the session) and the
// tool windows (targeting the application).
BMenu *CreateDebugMenu(const BMessenger &session);
BMenu *CreateWindowsMenu(int32 sessionId);

// A tool window of the session, running hidden so it receives notices.
BWindow *CreateToolWindow(ToolWindowKind kind, const SessionContext &context);

// Breakpoint marks on a line: removes them or adds an enabled one, and
// switches between enabled and disabled.
void ToggleBreakpoint(CodeEdit::TextModel &model, size_t line);
void ToggleBreakpointEnabled(CodeEdit::TextModel &model, size_t line);


// A column list for tool windows, sending selection and invocation
// messages to the window.
BColumnListView *CreateColumnList(const char *name, uint32 selectionWhat, uint32 invocationWhat);
// Leaf of a path.
std::string FileName(const std::string &path);


// A floating window above the application's windows that hides when
// closed. It watches the session from creation, and another one after
// SetContext(); without a session (an invalid messenger), it is empty.
class ToolWindow: public BWindow {
private:
	std::string fBaseTitle;

	void Watch(uint32 what);

protected:
	SessionContext fContext;

	const BMessenger &Session() const {return fContext.session;}
	// The session changed: the window forgets what it showed; the new
	// session's notices follow.
	virtual void SessionChanged() {}

public:
	ToolWindow(BRect frame, const char *title, const SessionContext &context);

	// Shows another session. Call with the window locked.
	void SetContext(const SessionContext &context);

	// Notices of other sessions (queued before a change) are dropped.
	void DispatchMessage(BMessage *message, BHandler *handler) override;
	bool QuitRequested() override;
};


} // Debugger
