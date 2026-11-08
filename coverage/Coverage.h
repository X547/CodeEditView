#pragma once

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <GraphicsDefs.h>

#include <CodeEditView/ViewTypes.h>


namespace CodeEdit {
class Rope;
}


// LLVM source-based coverage (llvm-cov export) for display.
namespace Coverage {


// Start of a region, which lasts until the next segment. Lines and columns
// are 1-based; columns count bytes.
struct Segment {
	size_t line {};
	size_t column {};
	uint64_t count {};
	bool hasCount {};
	bool isRegionEntry {};
	bool isGap {};
};


// A condition and how often it was true and false. The end column is
// exclusive.
struct Branch {
	size_t line {};
	size_t column {};
	size_t endLine {};
	size_t endColumn {};
	uint64_t trueCount {};
	uint64_t falseCount {};
};


struct Counts {
	uint64_t count {};
	uint64_t covered {};

	// Nullopt if there is nothing to cover.
	std::optional<double> Percent() const;
};


struct FileSummary {
	std::string path;
	Counts lines;
	Counts regions;
	Counts branches;
};


struct Summary {
	std::vector<FileSummary> files;
	FileSummary totals;
};


struct FileCoverage {
	std::vector<Segment> segments;
	std::vector<Branch> branches;
};


enum class LineState : uint8_t {
	// No code (comments, declarations, skipped preprocessor blocks).
	None,
	Covered,
	Uncovered,
};


struct Colors {
	rgb_color covered;
	rgb_color uncovered;
	rgb_color uncoveredRegion;
	rgb_color partialBranch;
};


// Parse the output of llvm-cov export; throw std::runtime_error if it is not
// what is expected.
Summary ParseSummary(std::string_view json);
// The coverage of the file with the path, or of the only file exported.
FileCoverage ParseFileCoverage(std::string_view json, std::string_view path);

// State of each line (index 0 is line 1), as llvm-cov decides it: a line is
// covered by the regions starting on it and the region continuing into it.
std::vector<LineState> ComputeLineStates(const std::vector<Segment> &segments, size_t lineCount);

Colors DefaultColors();
// "lines 85.2 % (120/141), regions ..., branches ...".
std::string FormatSummary(const FileSummary &summary);
// Line backgrounds for covered and uncovered lines, ranges for regions not
// executed inside executed lines and for branches taken only one way.
std::vector<CodeEdit::Decoration> MakeDecorations(
	const FileCoverage &coverage,
	const std::vector<LineState> &lines,
	const CodeEdit::Rope &text,
	const Colors &colors
);


} // Coverage
