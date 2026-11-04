#pragma once

#include <stddef.h>

#include <Messenger.h>
#include <Window.h>

#include <CodeEditView/TextModel.h>

#include "Protocol.h"


class BMenu;


namespace Debugger {


// The session of the application.
BMessenger SessionMessenger();
// Set while the application quits, so tool windows close instead of hiding.
extern bool gQuitting;

// Menus shared by the windows: run control (targeting the session) and the
// tool windows (targeting the application).
BMenu *CreateDebugMenu();
BMenu *CreateWindowsMenu();

// Breakpoint marks on a line: removes them or adds an enabled one, and
// switches between enabled and disabled.
void ToggleBreakpoint(CodeEdit::TextModel &model, size_t line);
void ToggleBreakpointEnabled(CodeEdit::TextModel &model, size_t line);


// A floating window above the application's windows that hides when
// closed. It watches the session from creation.
class ToolWindow: public BWindow {
public:
	ToolWindow(BRect frame, const char *title);

	bool QuitRequested() override;
};


} // Debugger
