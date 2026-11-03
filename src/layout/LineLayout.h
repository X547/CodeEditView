#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>


namespace CodeEdit {


struct LayoutOptions {
	uint32_t tabWidth = 4;
	// Cell width of East Asian Ambiguous characters (1 or 2).
	uint32_t ambiguousWidth = 1;
};


enum class ClusterKind: uint8_t {
	Text,
	Tab,
	// Control or invisible format character, drawn as PlaceholderText().
	Placeholder,
	// Byte that is not part of valid UTF-8, drawn as PlaceholderText().
	InvalidByte,
};


// Smallest caret-addressable unit of a line: a base character with its
// combining marks, variation selectors and joined emoji, a tab, or a
// placeholder.
struct Cluster {
	uint32_t offset {};
	uint32_t length {};
	uint32_t column {};
	uint32_t width {};
	// Base code point, or the byte value for InvalidByte.
	char32_t codePoint {};
	ClusterKind kind {};

	uint32_t EndOffset() const {return offset + length;}
	uint32_t EndColumn() const {return column + width;}
};


// Cell layout of one line (without its newline). Offsets are bytes relative to
// the line start.
class LineLayout {
private:
	std::vector<Cluster> fClusters;
	uint32_t fLength {};
	uint32_t fWidth {};

public:
	LineLayout() = default;
	LineLayout(std::string_view line, const LayoutOptions &options);

	const std::vector<Cluster> &Clusters() const {return fClusters;}
	uint32_t Length() const {return fLength;}
	uint32_t Width() const {return fWidth;}

	// Index of the cluster containing offset; Clusters().size() at Length().
	size_t ClusterAtOffset(size_t offset) const;
	// Index of the cluster covering column; Clusters().size() past the end.
	size_t ClusterAtColumn(uint32_t column) const;

	bool IsBoundary(size_t offset) const;
	size_t PreviousBoundary(size_t offset) const;
	size_t NextBoundary(size_t offset) const;
	// Start column of the cluster containing offset; Width() at Length().
	uint32_t ColumnOfOffset(size_t offset) const;
	// Cluster boundary closest to a fractional column, e.g. a mouse position.
	size_t BoundaryNearColumn(float column) const;
};


std::string PlaceholderText(const Cluster &cluster);


} // CodeEdit
