#pragma once

#include <stddef.h>

#include <GraphicsDefs.h>


namespace CodeEdit {


// Who made a fold: the user (commands, gutter), or the host application
// (CodeEditView::SetHostFolds()).
enum class FoldSource {
	User,
	Host,
};


// A fold hides the lines after its header line up to lastLine.
struct FoldRange {
	size_t headerLine {};
	size_t lastLine {};
	FoldSource source = FoldSource::User;

	bool operator==(const FoldRange &other) const = default;
};


// A fold region of the host application (CodeEditView::SetHostFolds()). It
// has a fold marker in the gutter like the folds of the syntax tree.
struct HostFold {
	size_t headerLine {};
	size_t lastLine {};
	bool folded = true;

	bool operator==(const HostFold &other) const = default;
};


enum class DecorationKind {
	// Tints whole lines: every line that [start, end) touches, or the line
	// of start if the range is empty.
	LineBackground,
	// Tints the text of [start, end), e.g. changed words.
	RangeBackground,
	// A horizontal line at start, which must be a line boundary: at the top
	// of the line starting there, or at the bottom of the last line if start
	// is the end of a text without a final newline.
	GapLine,
};


// Highlight set by the host application (CodeEditView::SetDecorations()).
// Offsets follow edits until the host replaces the decorations.
struct Decoration {
	DecorationKind kind = DecorationKind::LineBackground;
	size_t start {};
	size_t end {};
	// Mixed into the background by its alpha (0 = invisible); GapLine is
	// drawn with the color as is.
	rgb_color color {};

	bool operator==(const Decoration &other) const = default;
};


// A vertical position in the text: a model line and the fraction of its
// height (wrapped lines have several rows).
struct LinePosition {
	size_t line {};
	float fraction {};
};


} // CodeEdit
