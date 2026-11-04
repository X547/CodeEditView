#pragma once

#include <stdint.h>

#include <GraphicsDefs.h>


namespace CodeEdit {


// Gutter icon of a mark kind, drawn with vector shapes.
enum class MarkShape {
	None,
	Circle,
	CircleOutline,
	Bookmark,
	Arrow,
};


// How marks of one kind are shown (CodeEditView::SetMarkStyle()).
struct MarkStyle {
	MarkShape shape = MarkShape::None;
	rgb_color color {};
	// Background of the marked line: lineBackground mixed into the document
	// background by its alpha (0 = none).
	rgb_color lineBackground {};
	// Marks of higher priority are drawn over lower ones and choose the line
	// background.
	int32_t priority {};
};


// Styles of the built-in kinds (kMarkBookmark etc.); MarkShape::None for
// other kinds.
MarkStyle DefaultMarkStyle(uint32_t kind);


} // CodeEdit
