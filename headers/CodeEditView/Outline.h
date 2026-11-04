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
};


// A definition found in the syntax tree, with the definitions nested in it.
// Offsets are bytes in the model text.
struct OutlineItem {
	OutlineKind kind {};
	// The name; functions add their parameter list, e.g. "Draw(BRect rect)".
	std::string label;
	size_t start {};
	size_t end {};
	size_t nameStart {};
	size_t nameEnd {};
	std::vector<OutlineItem> children;
};


} // CodeEdit
