#include "Coverage.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <CodeEditView/Rope.h>

#include "Json.h"


namespace Coverage {


using CodeEdit::Decoration;
using CodeEdit::DecorationKind;
using CodeEdit::Rope;
using Debugger::JsonValue;


std::optional<double> Counts::Percent() const
{
	if (count == 0) {
		return std::nullopt;
	}
	return 100.0 * static_cast<double>(covered) / static_cast<double>(count);
}


static JsonValue ParseJson(std::string_view json)
{
	std::optional<JsonValue> root = JsonValue::Parse(json);
	if (!root.has_value()) {
		throw std::runtime_error("llvm-cov output is not valid JSON");
	}
	return std::move(*root);
}


// The "files" of the first export object.
static const JsonValue::Array &ExportedFiles(const JsonValue &root)
{
	const JsonValue &files = root["data"][0]["files"];
	if (!files.IsArray()) {
		throw std::runtime_error("llvm-cov output has no files");
	}
	return files.AsArray();
}


static Counts ParseCounts(const JsonValue &value)
{
	return {static_cast<uint64_t>(value["count"].AsInt()), static_cast<uint64_t>(value["covered"].AsInt())};
}


static void ParseSummaryCounts(const JsonValue &summary, FileSummary &file)
{
	file.lines = ParseCounts(summary["lines"]);
	file.regions = ParseCounts(summary["regions"]);
	file.branches = ParseCounts(summary["branches"]);
}


Summary ParseSummary(std::string_view json)
{
	const JsonValue root = ParseJson(json);
	Summary summary;
	for (const JsonValue &file: ExportedFiles(root)) {
		FileSummary &entry = summary.files.emplace_back();
		entry.path = file["filename"].AsString();
		ParseSummaryCounts(file["summary"], entry);
	}
	ParseSummaryCounts(root["data"][0]["totals"], summary.totals);
	return summary;
}


FileCoverage ParseFileCoverage(std::string_view json, std::string_view path)
{
	const JsonValue root = ParseJson(json);
	const JsonValue::Array &files = ExportedFiles(root);
	const JsonValue *file = nullptr;
	for (const JsonValue &candidate: files) {
		if (candidate["filename"].AsString() == path) {
			file = &candidate;
			break;
		}
	}
	if (file == nullptr && files.size() == 1) {
		file = &files[0];
	}
	if (file == nullptr) {
		throw std::runtime_error("llvm-cov has no coverage for " + std::string(path));
	}

	FileCoverage coverage;
	for (const JsonValue &value: (*file)["segments"].AsArray()) {
		Segment &segment = coverage.segments.emplace_back();
		segment.line = static_cast<size_t>(value[0].AsInt());
		segment.column = static_cast<size_t>(value[1].AsInt());
		segment.count = static_cast<uint64_t>(value[2].AsInt());
		segment.hasCount = value[3].AsBool();
		segment.isRegionEntry = value[4].AsBool();
		segment.isGap = value[5].AsBool();
	}
	for (const JsonValue &value: (*file)["branches"].AsArray()) {
		Branch &branch = coverage.branches.emplace_back();
		branch.line = static_cast<size_t>(value[0].AsInt());
		branch.column = static_cast<size_t>(value[1].AsInt());
		branch.endLine = static_cast<size_t>(value[2].AsInt());
		branch.endColumn = static_cast<size_t>(value[3].AsInt());
		branch.trueCount = static_cast<uint64_t>(value[4].AsInt());
		branch.falseCount = static_cast<uint64_t>(value[5].AsInt());
	}
	return coverage;
}


// LineCoverageStats of LLVM's CoverageMapping.cpp.
static LineState StateOfLine(const std::vector<const Segment*> &lineSegments, const Segment *wrapped)
{
	const auto isStartOfRegion = [](const Segment *segment) {
		return !segment->isGap && segment->hasCount && segment->isRegionEntry;
	};
	size_t regionStarts = 0;
	for (const Segment *segment: lineSegments) {
		if (isStartOfRegion(segment)) {
			regionStarts++;
		}
	}
	const bool startOfSkippedRegion = !lineSegments.empty()
		&& !lineSegments.front()->hasCount && lineSegments.front()->isRegionEntry;
	const bool mapped = !startOfSkippedRegion && ((wrapped != nullptr && wrapped->hasCount) || regionStarts > 0);
	if (!mapped) {
		return LineState::None;
	}

	uint64_t count = wrapped != nullptr ? wrapped->count : 0;
	for (const Segment *segment: lineSegments) {
		if (isStartOfRegion(segment)) {
			count = std::max(count, segment->count);
		}
	}
	return count > 0 ? LineState::Covered : LineState::Uncovered;
}


std::vector<LineState> ComputeLineStates(const std::vector<Segment> &segments, size_t lineCount)
{
	std::vector<LineState> states(lineCount, LineState::None);
	std::vector<const Segment*> lineSegments;
	const Segment *wrapped = nullptr;
	size_t next = 0;
	for (size_t line = 1; line <= lineCount && next < segments.size(); line++) {
		if (!lineSegments.empty()) {
			wrapped = lineSegments.back();
		}
		lineSegments.clear();
		while (next < segments.size() && segments[next].line < line) {
			wrapped = &segments[next++];
		}
		while (next < segments.size() && segments[next].line == line) {
			lineSegments.push_back(&segments[next++]);
		}
		states[line - 1] = StateOfLine(lineSegments, wrapped);
	}
	return states;
}


Colors DefaultColors()
{
	return {
		{0, 180, 0, 45},
		{230, 0, 0, 55},
		{230, 0, 0, 120},
		{255, 140, 0, 150},
	};
}


// Byte offset of a 1-based line and column, clamped to the text and to the
// end of the line.
static size_t OffsetOf(const Rope &text, size_t line, size_t column)
{
	if (line == 0) {
		return 0;
	}
	if (line > text.LineCount()) {
		return text.Length();
	}
	const size_t start = text.LineStart(line - 1);
	return std::min(start + (column > 0 ? column - 1 : 0), text.LineEnd(line - 1));
}


static void AddLineBackgrounds(
	const std::vector<LineState> &lines,
	const Rope &text,
	const Colors &colors,
	std::vector<Decoration> &decorations
)
{
	const size_t lineCount = std::min(lines.size(), text.LineCount());
	size_t first = 0;
	while (first < lineCount) {
		const LineState state = lines[first];
		size_t last = first;
		while (last + 1 < lineCount && lines[last + 1] == state) {
			last++;
		}
		if (state != LineState::None) {
			// The decoration covers the lines up to the one holding end - 1.
			const size_t start = text.LineStart(first);
			const size_t end = last + 1 < text.LineCount() ? text.LineStart(last + 1) : text.Length();
			decorations.push_back({
				DecorationKind::LineBackground,
				start,
				std::max(start, end),
				state == LineState::Covered ? colors.covered : colors.uncovered
			});
		}
		first = last + 1;
	}
}


// Regions with a count of 0 where they lie on executed lines; on lines not
// executed the line background shows it already.
static void AddUncoveredRegions(
	const FileCoverage &coverage,
	const std::vector<LineState> &lines,
	const Rope &text,
	const Colors &colors,
	std::vector<Decoration> &decorations
)
{
	const std::vector<Segment> &segments = coverage.segments;
	for (size_t i = 0; i < segments.size(); i++) {
		const Segment &segment = segments[i];
		if (!segment.hasCount || segment.count != 0 || segment.isGap) {
			continue;
		}
		const size_t start = OffsetOf(text, segment.line, segment.column);
		const size_t end = i + 1 < segments.size()
			? OffsetOf(text, segments[i + 1].line, segments[i + 1].column)
			: text.Length();
		if (end <= start) {
			continue;
		}
		const size_t lastLine = text.LineOfOffset(end - 1);
		for (size_t line = text.LineOfOffset(start); line <= lastLine && line < lines.size(); line++) {
			if (lines[line] != LineState::Covered) {
				continue;
			}
			const size_t pieceStart = std::max(start, text.LineStart(line));
			const size_t pieceEnd = std::min(end, text.LineEnd(line));
			if (pieceEnd > pieceStart) {
				decorations.push_back({DecorationKind::RangeBackground, pieceStart, pieceEnd, colors.uncoveredRegion});
			}
		}
	}
}


static void AddPartialBranches(
	const FileCoverage &coverage,
	const Rope &text,
	const Colors &colors,
	std::vector<Decoration> &decorations
)
{
	for (const Branch &branch: coverage.branches) {
		// Neither side taken: the line shows that it did not run.
		if ((branch.trueCount == 0) == (branch.falseCount == 0)) {
			continue;
		}
		const size_t start = OffsetOf(text, branch.line, branch.column);
		const size_t end = OffsetOf(text, branch.endLine, branch.endColumn);
		if (end > start) {
			decorations.push_back({DecorationKind::RangeBackground, start, end, colors.partialBranch});
		}
	}
}


std::vector<Decoration> MakeDecorations(
	const FileCoverage &coverage,
	const std::vector<LineState> &lines,
	const Rope &text,
	const Colors &colors
)
{
	std::vector<Decoration> decorations;
	AddLineBackgrounds(lines, text, colors, decorations);
	AddUncoveredRegions(coverage, lines, text, colors, decorations);
	AddPartialBranches(coverage, text, colors, decorations);
	return decorations;
}


} // Coverage
