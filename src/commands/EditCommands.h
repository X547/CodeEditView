#pragma once

#include <CodeEditView/EditorCommands.h>


namespace CodeEdit {


namespace EditorCommands {


// Undo, redo, the clipboard and select all: handled by every host, after the
// registered sets.
EditorCommandSet &BuiltInEditCommands();


} // EditorCommands


} // CodeEdit
