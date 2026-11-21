#pragma once

#include <CodeEditView/EditorCommands.h>


namespace CodeEdit {


namespace EditorCommands {


// Command sets handled by every host, after the registered sets: undo, redo,
// the clipboard and select all; shift, folding and bookmarks.
EditorCommandSet &BuiltInEditCommands();
EditorCommandSet &BuiltInViewCommands();


} // EditorCommands


} // CodeEdit
