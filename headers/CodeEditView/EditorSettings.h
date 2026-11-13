#pragma once

#include <stdint.h>


namespace CodeEdit {


struct EditorSettings {
	uint32_t tabWidth = 4;
	// Width of one indentation level; 0 means tabWidth (editorconfig's
	// indent_size). Used when indenting with spaces.
	uint32_t indentSize = 0;
	// Indent with spaces instead of tabs (editorconfig's indent_style).
	bool insertSpacesForTab = false;
	// Enter copies the leading whitespace of the current line.
	bool autoIndent = true;
	// Home toggles between the first non-blank character and the line start.
	bool smartHome = true;
	// Cell width of East Asian Ambiguous characters (1 or 2).
	uint32_t ambiguousWidth = 1;
	// Rows kept visible around the caret when navigating with the keyboard.
	uint32_t contextLines = 3;
	// Wrap long lines at the view width.
	bool wordWrap = false;
	// Continuation rows are indented by the leading whitespace of their line
	// plus this many cells.
	uint32_t wrapIndent = 4;
	// The view does not change the text: no typing, deleting, pasting,
	// dropping, undo or input method. Selecting and copying still work. Per
	// view, so other views of the same model may still edit it.
	bool readOnly = false;
	// A vertical guide line at the left edge of this column; 0 for none.
	uint32_t marginColumn = 0;
	// Typing a character that ends past this column breaks the line at the
	// last blank that leaves the text before it within the column, like a
	// text width setting; 0 for none. Lines before hardWrapFirstLine are not
	// broken, e.g. the subject line of a commit message.
	uint32_t hardWrapColumn = 0;
	uint32_t hardWrapFirstLine = 0;
	// Header lines of the scopes holding the top of the view stay at the top
	// (sticky scroll); at most this many, 0 for none. The scopes are those
	// that can be folded.
	uint32_t stickyLines = 0;

	bool operator==(const EditorSettings &other) const = default;
};


} // CodeEdit
