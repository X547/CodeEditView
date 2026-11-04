#include "LineLayout.h"

#include <stdio.h>

#include <algorithm>
#include <stdexcept>

#include "unicode/CharWidth.h"
#include "unicode/Utf8.h"


namespace CodeEdit {


struct ClusterExtent {
	size_t end;
	uint32_t width;
};


static bool IsPlaceholderClass(CharClass charClass)
{
	return charClass == CharClass::Control || charClass == CharClass::Format;
}


// Extends a text cluster starting with base at pos over the characters that
// attach to it, and computes its cell width.
static ClusterExtent ExtendCluster(
	std::string_view line,
	size_t pos,
	const Utf8Char &base,
	const CharProperties &baseProperties,
	const LayoutOptions &options
)
{
	uint32_t width = 1;
	switch (baseProperties.charClass) {
		case CharClass::Wide:
			width = 2;
			break;
		case CharClass::Ambiguous:
			width = options.ambiguousWidth;
			break;
		default:
			break;
	}

	const bool isRegional = IsRegionalIndicator(base.codePoint);
	bool isRegionalPair = false;
	bool hasEmojiVariation = false;
	bool hasTextVariation = false;
	bool joinNext = false;
	size_t end = pos + base.length;
	while (end < line.size()) {
		const Utf8Char next = DecodeUtf8(line, end);
		if (!next.valid || next.codePoint == '\t') {
			break;
		}
		const CharProperties nextProperties = GetCharProperties(next.codePoint);
		if (IsPlaceholderClass(nextProperties.charClass)) {
			break;
		}

		bool extend = false;
		if (joinNext) {
			extend = true;
			joinNext = false;
		} else if (next.codePoint == kZeroWidthJoiner) {
			extend = true;
			joinNext = baseProperties.emoji;
		} else if (nextProperties.charClass == CharClass::Combining) {
			extend = true;
			hasEmojiVariation = hasEmojiVariation || next.codePoint == kEmojiVariationSelector;
			hasTextVariation = hasTextVariation || next.codePoint == kTextVariationSelector;
		} else if (baseProperties.emoji && IsEmojiModifier(next.codePoint)) {
			extend = true;
		} else if (isRegional && !isRegionalPair && IsRegionalIndicator(next.codePoint)) {
			extend = true;
			isRegionalPair = true;
		}
		if (!extend) {
			break;
		}
		end += next.length;
	}

	if (baseProperties.emoji && hasEmojiVariation) {
		width = 2;
	} else if (baseProperties.emojiPresentation && hasTextVariation) {
		width = 1;
	}
	if (isRegionalPair) {
		width = 2;
	}
	return {end, width};
}


//#pragma mark - LineLayout

LineLayout::LineLayout(std::string_view line, const LayoutOptions &options)
{
	if (line.size() > UINT32_MAX) {
		throw std::length_error("LineLayout: line too long");
	}
	fLength = static_cast<uint32_t>(line.size());

	uint32_t column = 0;
	size_t pos = 0;
	while (pos < line.size()) {
		const Utf8Char ch = DecodeUtf8(line, pos);
		Cluster cluster;
		cluster.offset = static_cast<uint32_t>(pos);
		cluster.length = ch.length;
		cluster.column = column;
		cluster.codePoint = ch.codePoint;

		if (!ch.valid) {
			cluster.kind = ClusterKind::InvalidByte;
		} else if (ch.codePoint == '\t') {
			cluster.kind = ClusterKind::Tab;
			cluster.width = options.tabWidth - column % options.tabWidth;
		} else {
			const CharProperties properties = GetCharProperties(ch.codePoint);
			if (IsPlaceholderClass(properties.charClass) || ch.codePoint == kZeroWidthJoiner) {
				cluster.kind = ClusterKind::Placeholder;
			} else {
				const ClusterExtent extent = ExtendCluster(line, pos, ch, properties, options);
				cluster.kind = ClusterKind::Text;
				cluster.length = static_cast<uint32_t>(extent.end - pos);
				cluster.width = extent.width;
			}
		}
		if (cluster.kind == ClusterKind::Placeholder || cluster.kind == ClusterKind::InvalidByte) {
			cluster.width = static_cast<uint32_t>(PlaceholderText(cluster).size());
		}

		fClusters.push_back(cluster);
		pos += cluster.length;
		column += cluster.width;
	}
	fWidth = column;
}


size_t LineLayout::ClusterAtOffset(size_t offset) const
{
	if (offset >= fLength) {
		return fClusters.size();
	}
	const auto next = std::upper_bound(
		fClusters.begin(),
		fClusters.end(),
		offset,
		[](size_t value, const Cluster &cluster) {
			return value < cluster.offset;
		}
	);
	return next - fClusters.begin() - 1;
}


size_t LineLayout::ClusterAtColumn(uint32_t column) const
{
	if (column >= fWidth) {
		return fClusters.size();
	}
	const auto next = std::upper_bound(
		fClusters.begin(),
		fClusters.end(),
		column,
		[](uint32_t value, const Cluster &cluster) {
			return value < cluster.column;
		}
	);
	return next - fClusters.begin() - 1;
}


bool LineLayout::IsBoundary(size_t offset) const
{
	if (offset >= fLength) {
		return offset == fLength;
	}
	return fClusters[ClusterAtOffset(offset)].offset == offset;
}


size_t LineLayout::PreviousBoundary(size_t offset) const
{
	if (offset == 0) {
		return 0;
	}
	offset = std::min<size_t>(offset, fLength);
	return fClusters[ClusterAtOffset(offset - 1)].offset;
}


size_t LineLayout::NextBoundary(size_t offset) const
{
	if (offset >= fLength) {
		return fLength;
	}
	return fClusters[ClusterAtOffset(offset)].EndOffset();
}


uint32_t LineLayout::ColumnOfOffset(size_t offset) const
{
	if (offset >= fLength) {
		return fWidth;
	}
	return fClusters[ClusterAtOffset(offset)].column;
}


size_t LineLayout::BoundaryNearColumn(float column) const
{
	if (column <= 0) {
		return 0;
	}
	const size_t index = ClusterAtColumn(static_cast<uint32_t>(column));
	if (index == fClusters.size()) {
		return fLength;
	}
	const Cluster &cluster = fClusters[index];
	if (column - cluster.column < cluster.width / 2.0f) {
		return cluster.offset;
	}
	return cluster.EndOffset();
}


std::string PlaceholderText(const Cluster &cluster)
{
	char buffer[16];
	if (cluster.kind == ClusterKind::InvalidByte) {
		snprintf(buffer, sizeof(buffer), "\\x%02X", static_cast<unsigned>(cluster.codePoint));
	} else if (cluster.codePoint < 0x20 || cluster.codePoint == 0x7f) {
		snprintf(buffer, sizeof(buffer), "^%c", static_cast<char>(cluster.codePoint ^ 0x40));
	} else {
		snprintf(buffer, sizeof(buffer), "<%04X>", static_cast<unsigned>(cluster.codePoint));
	}
	return buffer;
}


//#pragma mark - Wrapping

static bool IsBlank(const Cluster &cluster)
{
	return cluster.kind == ClusterKind::Tab
		|| (cluster.kind == ClusterKind::Text && (cluster.codePoint == ' ' || cluster.codePoint == 0x3000));
}


static bool IsAsciiPunctuation(char32_t codePoint)
{
	return codePoint < 0x80 && codePoint > ' ' && codePoint != 0x7f
		&& !(codePoint >= '0' && codePoint <= '9')
		&& !(codePoint >= 'A' && codePoint <= 'Z')
		&& !(codePoint >= 'a' && codePoint <= 'z')
		&& codePoint != '_';
}


// Punctuation a row may end with when a word follows.
static bool IsBreakAfterPunctuation(char32_t codePoint)
{
	switch (codePoint) {
		case ',': case ';': case ':': case '.':
		case '(': case '[': case '{':
		case ')': case ']': case '}':
		case '=': case '&': case '|': case '+': case '-': case '*': case '/':
		case '%': case '<': case '>': case '!': case '?':
			return true;
		default:
			break;
	}
	return false;
}


// Full-width punctuation that must not start a row.
static bool IsNoBreakBefore(char32_t codePoint)
{
	switch (codePoint) {
		case 0x3001: case 0x3002: case 0xff0c: case 0xff0e: case 0xff1a: case 0xff1b:
		case 0xff01: case 0xff1f: case 0xff09: case 0x300d: case 0x300f: case 0x3011:
		case 0x3015: case 0x3009: case 0x300b: case 0x30fc:
			return true;
		default:
			break;
	}
	return false;
}


static bool CanBreakBefore(const std::vector<Cluster> &clusters, size_t index)
{
	const Cluster &previous = clusters[index - 1];
	const Cluster &current = clusters[index];
	if (IsBlank(current)) {
		return false;
	}
	if (IsBlank(previous)) {
		return true;
	}
	if (previous.kind != ClusterKind::Text || current.kind != ClusterKind::Text) {
		return true;
	}
	if ((previous.width == 2 || current.width == 2) && !IsNoBreakBefore(current.codePoint)) {
		return true;
	}
	return IsBreakAfterPunctuation(previous.codePoint) && !IsAsciiPunctuation(current.codePoint);
}


std::vector<WrapRow> WrapLine(const LineLayout &layout, uint32_t width, uint32_t continuationIndent)
{
	std::vector<WrapRow> rows {WrapRow {}};
	const std::vector<Cluster> &clusters = layout.Clusters();
	if (width == 0 || layout.Width() <= width) {
		return rows;
	}

	uint32_t leading = 0;
	for (const Cluster &cluster : clusters) {
		if (!IsBlank(cluster)) {
			break;
		}
		leading = cluster.EndColumn();
	}
	const uint32_t indent = std::min(leading + continuationIndent, width / 2);

	const size_t count = clusters.size();
	size_t start = 0;
	uint32_t rowIndent = 0;
	for (;;) {
		const uint32_t limit = clusters[start].column + (width - rowIndent);
		size_t end = start;
		while (end < count && clusters[end].EndColumn() <= limit) {
			end++;
		}
		if (end == count) {
			break;
		}
		if (end == start) {
			end = start + 1;
		} else if (IsBlank(clusters[end])) {
			while (end < count && IsBlank(clusters[end])) {
				end++;
			}
		} else {
			size_t breakAt = end;
			while (breakAt > start + 1 && !CanBreakBefore(clusters, breakAt)) {
				breakAt--;
			}
			if (CanBreakBefore(clusters, breakAt)) {
				end = breakAt;
			}
		}
		if (end == count) {
			break;
		}
		rows.push_back({clusters[end].offset, clusters[end].column, indent});
		start = end;
		rowIndent = indent;
	}
	return rows;
}


} // CodeEdit
