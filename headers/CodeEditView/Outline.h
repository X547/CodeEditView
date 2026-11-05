#pragma once

#include <stddef.h>

#include <string>
#include <vector>


namespace CodeEdit {


enum class OutlineKind {
	Namespace,
	Class,
	Struct,
	Union,
	Enum,
	Function,
	// A named "#pragma mark" (also as a "//" comment); holds the items after
	// it up to the next mark, the end of its preprocessor branch or the end
	// of the enclosing item.
	Section,
	// An unnamed "#pragma mark -": a divider with no children and an empty
	// range (start == end).
	Separator,
};


// A definition found in the syntax tree, with the definitions nested in it.
// Offsets are bytes in the model text.
struct OutlineItem {
	OutlineKind kind {};
	// The name; functions add their parameter list, e.g. "Draw(BRect rect)".
	// For a section the mark text without surrounding spaces and dashes.
	std::string label;
	size_t start {};
	size_t end {};
	size_t nameStart {};
	size_t nameEnd {};
	std::vector<OutlineItem> children;
};


} // CodeEdit
