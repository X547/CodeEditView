#pragma once

#include <stdint.h>


namespace CodeEdit {


struct EditorSettings {
	uint32_t tabWidth = 4;
	bool insertSpacesForTab = false;
	// Enter copies the leading whitespace of the current line.
	bool autoIndent = true;
	// Home toggles between the first non-blank character and the line start.
	bool smartHome = true;
	// Cell width of East Asian Ambiguous characters (1 or 2).
	uint32_t ambiguousWidth = 1;
	// Rows kept visible around the caret when navigating with the keyboard.
	uint32_t contextLines = 3;
};


} // CodeEdit
