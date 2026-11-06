#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/Rope.h>

#include "Coverage.h"
#include "LlvmCov.h"


namespace Coverage {


using CodeEdit::Decoration;
using CodeEdit::DecorationKind;
using CodeEdit::Rope;


// int f(int x)              1
// {                         2
// 	if (x > 0)               3
// 		return 1;            4
// 	return 0;                5
// }                         6
//                           7
// // end                    8
static const char *kSource = "int f(int x)\n{\n\tif (x > 0)\n\t\treturn 1;\n\treturn 0;\n}\n\n// end\n";

// As llvm-cov exports it: the body, the condition, a gap region after the
// condition (count of the then branch), the then branch, the rest of the
// body, the end of the function. The condition was never true.
static const char *kFileJson = R"({"data": [{"files": [
	{"filename": "/other.c", "segments": [], "branches": []},
	{"filename": "/src/f.c", "segments": [
		[2, 1, 5, true, true, false],
		[3, 6, 5, true, true, false],
		[3, 11, 0, true, true, true],
		[4, 3, 0, true, true, false],
		[4, 12, 5, true, false, false],
		[6, 2, 0, false, false, false]
	], "branches": [
		[3, 6, 3, 11, 0, 5, 0, 0, 4]
	]}
]}], "type": "llvm.coverage.json.export", "version": "3.0.1"})";


static std::vector<LineState> States(const FileCoverage &coverage)
{
	return ComputeLineStates(coverage.segments, Rope(kSource).LineCount());
}


TEST_CASE("the coverage of the requested file is parsed")
{
	const FileCoverage coverage = ParseFileCoverage(kFileJson, "/src/f.c");
	REQUIRE(coverage.segments.size() == 6);
	CHECK(coverage.segments[2].isGap);
	CHECK(coverage.segments[4].count == 5);
	CHECK_FALSE(coverage.segments[4].isRegionEntry);
	REQUIRE(coverage.branches.size() == 1);
	CHECK(coverage.branches[0].trueCount == 0);
	CHECK(coverage.branches[0].falseCount == 5);
	CHECK(coverage.branches[0].endColumn == 11);

	CHECK_THROWS_AS(ParseFileCoverage(kFileJson, "/missing.c"), std::runtime_error);
	CHECK_THROWS_AS(ParseFileCoverage("{", "/src/f.c"), std::runtime_error);
	CHECK_THROWS_AS(ParseFileCoverage("{}", "/src/f.c"), std::runtime_error);
}


TEST_CASE("line states follow llvm-cov")
{
	const std::vector<LineState> states = States(ParseFileCoverage(kFileJson, "/src/f.c"));
	REQUIRE(states.size() == 9);
	CHECK(states[0] == LineState::None);
	CHECK(states[1] == LineState::Covered);
	CHECK(states[2] == LineState::Covered);
	// Entered through the gap region, whose count is 0.
	CHECK(states[3] == LineState::Uncovered);
	CHECK(states[4] == LineState::Covered);
	CHECK(states[5] == LineState::Covered);
	CHECK(states[6] == LineState::None);
	CHECK(states[7] == LineState::None);
}


TEST_CASE("skipped regions have no state")
{
	// #if 0 block on lines 2-3: a region entry without a count; the count
	// resumes at the end of the #endif line.
	const std::vector<Segment> segments {
		{1, 1, 2, true, true, false},
		{2, 1, 0, false, true, false},
		{3, 7, 2, true, false, false},
		{5, 1, 0, false, false, false},
	};
	const std::vector<LineState> states = ComputeLineStates(segments, 6);
	CHECK(states[0] == LineState::Covered);
	CHECK(states[1] == LineState::None);
	CHECK(states[2] == LineState::None);
	CHECK(states[3] == LineState::Covered);
}


TEST_CASE("line decorations join runs of lines")
{
	const Rope text(kSource);
	const FileCoverage coverage = ParseFileCoverage(kFileJson, "/src/f.c");
	const Colors colors = DefaultColors();
	const std::vector<Decoration> decorations = MakeDecorations(coverage, States(coverage), text, colors);

	std::vector<Decoration> lines;
	for (const Decoration &decoration: decorations) {
		if (decoration.kind == DecorationKind::LineBackground) {
			lines.push_back(decoration);
		}
	}
	REQUIRE(lines.size() == 3);
	CHECK(lines[0].start == text.LineStart(1));
	CHECK(lines[0].end == text.LineStart(3));
	CHECK(lines[0].color == colors.covered);
	CHECK(lines[1].start == text.LineStart(3));
	CHECK(lines[1].end == text.LineStart(4));
	CHECK(lines[1].color == colors.uncovered);
	CHECK(lines[2].start == text.LineStart(4));
	CHECK(lines[2].end == text.LineStart(6));
}


TEST_CASE("partial branches and unexecuted regions on executed lines are marked")
{
	// The "1" of the conditional expression never ran; x was never 0.
	const Rope text("int g(int x) {return x ? 1 : 2;}");
	FileCoverage coverage;
	coverage.segments = {
		{1, 14, 3, true, true, false},
		{1, 26, 0, true, true, false},
		{1, 27, 3, true, false, false},
		{1, 33, 0, false, false, false},
	};
	coverage.branches = {{1, 22, 1, 23, 3, 0}};
	const Colors colors = DefaultColors();
	const std::vector<LineState> states = ComputeLineStates(coverage.segments, text.LineCount());
	REQUIRE(states[0] == LineState::Covered);

	const std::vector<Decoration> decorations = MakeDecorations(coverage, states, text, colors);
	REQUIRE(decorations.size() == 3);
	CHECK(decorations[1].kind == DecorationKind::RangeBackground);
	CHECK(text.Substring(decorations[1].start, decorations[1].end - decorations[1].start) == "1");
	CHECK(decorations[1].color == colors.uncoveredRegion);
	CHECK(text.Substring(decorations[2].start, decorations[2].end - decorations[2].start) == "x");
	CHECK(decorations[2].color == colors.partialBranch);
}


TEST_CASE("an unexecuted region on unexecuted lines needs no range")
{
	const Rope text(kSource);
	const FileCoverage coverage = ParseFileCoverage(kFileJson, "/src/f.c");
	for (const Decoration &decoration: MakeDecorations(coverage, States(coverage), text, DefaultColors())) {
		if (decoration.kind == DecorationKind::RangeBackground) {
			// Only the condition, which was never true.
			CHECK(text.Substring(decoration.start, decoration.end - decoration.start) == "x > 0");
		}
	}
}


TEST_CASE("the summary lists files and totals")
{
	const Summary summary = ParseSummary(R"({"data": [{"files": [
		{"filename": "/src/a.c", "summary": {
			"lines": {"count": 10, "covered": 4},
			"regions": {"count": 8, "covered": 8},
			"branches": {"count": 0, "covered": 0}
		}}
	], "totals": {
		"lines": {"count": 10, "covered": 4},
		"regions": {"count": 8, "covered": 8},
		"branches": {"count": 0, "covered": 0}
	}}]})");
	REQUIRE(summary.files.size() == 1);
	CHECK(summary.files[0].path == "/src/a.c");
	CHECK(summary.files[0].lines.Percent() == 40.0);
	CHECK(summary.files[0].regions.Percent() == 100.0);
	CHECK_FALSE(summary.files[0].branches.Percent().has_value());
	CHECK(summary.totals.lines.count == 10);
}


TEST_CASE("programs run without fork and report failures")
{
	CHECK(RunProgram({"echo", "hello", "world"}) == "hello world\n");
	try {
		RunProgram({"sh", "-c", "echo out; echo broken >&2; exit 3"});
		FAIL("no exception");
	} catch (const std::runtime_error &error) {
		CHECK(std::string(error.what()) == "broken");
	}
	CHECK_THROWS_AS(RunProgram({"no-such-program-for-coverage-test"}), std::runtime_error);
}


} // Coverage
