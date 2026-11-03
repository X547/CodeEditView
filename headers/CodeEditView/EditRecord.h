#pragma once

#include <stddef.h>
#include <stdint.h>


namespace CodeEdit {


// Line and byte column, as used by TreeSitter's TSPoint.
struct TextPoint {
	size_t line {};
	size_t column {};

	bool operator==(const TextPoint &other) const = default;
};


// Side an offset sticks to when text is inserted exactly at it, or when a
// range strictly containing it is replaced. Offsets at the boundaries of a
// replaced range stay on their side regardless of gravity.
enum class Gravity {
	Left,
	Right,
};


// One replacement of [offset, offset + oldLength) with newLength bytes.
struct EditRecord {
	uint64_t revision {};
	size_t offset {};
	size_t oldLength {};
	size_t newLength {};
	TextPoint start;
	TextPoint oldEnd;
	TextPoint newEnd;

	size_t OldEnd() const {return offset + oldLength;}
	size_t NewEnd() const {return offset + newLength;}

	// Maps an offset valid before the edit to one valid after it.
	size_t MapOffset(size_t position, Gravity gravity) const;
};


} // CodeEdit
