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

	bool operator==(const EditorSettings &other) const = default;
};


} // CodeEdit
