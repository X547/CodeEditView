#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string_view>
#include <vector>


namespace CodeEdit {
class TextModel;
}


namespace DiffKit {


// A run of lines that differ between side A and side B. Lines are 0-based
// model lines. A side with no lines (count 0) marks where the other side's
// lines would go: before line first.
struct Chunk {
	size_t aFirst {};
	size_t aCount {};
	size_t bFirst {};
	size_t bCount {};

	bool operator==(const Chunk &other) const = default;
};


enum class DiffAlgorithm {
	Myers,
	Minimal,
	Patience,
};


// Inclusive range of model lines.
struct LineRange {
	size_t first {};
	size_t last {};
};


std::vector<Chunk> ComputeChunks(std::string_view a, std::string_view b, DiffAlgorithm algorithm = DiffAlgorithm::Myers);

// Maps a fractional line position (line + fraction of it) from one side to
// the other: one to one between chunks, proportionally inside them.
double MapLine(const std::vector<Chunk> &chunks, double line, bool fromA);

// The chunks with sides A and B exchanged, to transfer the other way.
std::vector<Chunk> SwapSides(const std::vector<Chunk> &chunks);

// Indexes of the chunks that a range of lines on one side touches. A chunk
// without lines on that side is touched if the range includes or borders
// its position.
std::vector<size_t> TouchedChunks(const std::vector<Chunk> &chunks, const LineRange &range, bool onA);

// Copies the B lines of the chunks to A, replacing their A lines; with
// selection (B lines), only the selected B lines are copied. One undo step
// in A named undoName. The chunks must be current.
void ApplyToA(CodeEdit::TextModel &a, CodeEdit::TextModel &b, const std::vector<Chunk> &chunks,
	const std::vector<size_t> &indexes, std::string_view undoName, const LineRange *selection = nullptr);

// Reverts B lines to A: the chunks' B lines are replaced by their A lines;
// with selection (B lines), unselected B lines stay after the A lines. One
// undo step in B named undoName. The chunks must be current.
void RevertB(CodeEdit::TextModel &a, CodeEdit::TextModel &b, const std::vector<Chunk> &chunks,
	const std::vector<size_t> &indexes, std::string_view undoName, const LineRange *selection = nullptr);


} // DiffKit
