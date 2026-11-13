#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "layout/LineLayout.h"


namespace CodeEdit {


struct ExpectedCluster {
	uint32_t offset;
	uint32_t length;
	uint32_t column;
	uint32_t width;
	ClusterKind kind;
};


static void CheckLayout(
	std::string_view line,
	std::initializer_list<ExpectedCluster> expected,
	const LayoutOptions &options = {}
)
{
	CAPTURE(line);
	const LineLayout layout(line, options);
	REQUIRE(layout.Clusters().size() == expected.size());
	uint32_t width = 0;
	size_t index = 0;
	for (const ExpectedCluster &cluster : expected) {
		CAPTURE(index);
		const Cluster &actual = layout.Clusters()[index];
		CHECK(actual.offset == cluster.offset);
		CHECK(actual.length == cluster.length);
		CHECK(actual.column == cluster.column);
		CHECK(actual.width == cluster.width);
		CHECK(actual.kind == cluster.kind);
		width += cluster.width;
		index++;
	}
	CHECK(layout.Width() == width);
	CHECK(layout.Length() == line.size());
}


TEST_CASE("layout of plain text, tabs and wide characters")
{
	CheckLayout("", {});
	CheckLayout("a\tb", {
		{0, 1, 0, 1, ClusterKind::Text},
		{1, 1, 1, 3, ClusterKind::Tab},
		{2, 1, 4, 1, ClusterKind::Text},
	});
	CheckLayout("\t\t", {
		{0, 1, 0, 8, ClusterKind::Tab},
		{1, 1, 8, 8, ClusterKind::Tab},
	}, {8, 1});
	CheckLayout("あい\t", {
		{0, 3, 0, 2, ClusterKind::Text},
		{3, 3, 2, 2, ClusterKind::Text},
		{6, 1, 4, 4, ClusterKind::Tab},
	});
	CheckLayout("é", {{0, 2, 0, 1, ClusterKind::Text}});
	CheckLayout("é", {{0, 2, 0, 2, ClusterKind::Text}}, {4, 2});
}


TEST_CASE("layout of combining sequences and emoji")
{
	CheckLayout("éx", {
		{0, 3, 0, 1, ClusterKind::Text},
		{3, 1, 1, 1, ClusterKind::Text},
	});
	CheckLayout("́a", {
		{0, 2, 0, 1, ClusterKind::Text},
		{2, 1, 1, 1, ClusterKind::Text},
	});
	CheckLayout("❤", {{0, 3, 0, 1, ClusterKind::Text}});
	CheckLayout("❤️", {{0, 6, 0, 2, ClusterKind::Text}});
	CheckLayout("1️⃣", {{0, 7, 0, 2, ClusterKind::Text}});
	CheckLayout("\U0001f600︎", {{0, 7, 0, 1, ClusterKind::Text}});
	CheckLayout("\U0001f468‍\U0001f469‍\U0001f467", {{0, 18, 0, 2, ClusterKind::Text}});
	CheckLayout("\U0001f44d\U0001f3fd", {{0, 8, 0, 2, ClusterKind::Text}});
	CheckLayout("\U0001f1ef\U0001f1f5\U0001f1fa\U0001f1f8", {
		{0, 8, 0, 2, ClusterKind::Text},
		{8, 8, 2, 2, ClusterKind::Text},
	});
	CheckLayout("a‍b", {
		{0, 4, 0, 1, ClusterKind::Text},
		{4, 1, 1, 1, ClusterKind::Text},
	});
	CheckLayout("각", {{0, 9, 0, 2, ClusterKind::Text}});
}


TEST_CASE("layout of placeholders")
{
	CheckLayout("\x01x", {
		{0, 1, 0, 2, ClusterKind::Placeholder},
		{1, 1, 2, 1, ClusterKind::Text},
	});
	CheckLayout("​", {{0, 3, 0, 6, ClusterKind::Placeholder}});
	CheckLayout("‍", {{0, 3, 0, 6, ClusterKind::Placeholder}});
	CheckLayout("a\xff", {
		{0, 1, 0, 1, ClusterKind::Text},
		{1, 1, 1, 4, ClusterKind::InvalidByte},
	});

	const LineLayout layout("\x01\x7f\u0085​\xfe", LayoutOptions {});
	REQUIRE(layout.Clusters().size() == 5);
	CHECK(PlaceholderText(layout.Clusters()[0]) == "^A");
	CHECK(PlaceholderText(layout.Clusters()[1]) == "^?");
	CHECK(PlaceholderText(layout.Clusters()[2]) == "<0085>");
	CHECK(PlaceholderText(layout.Clusters()[3]) == "<200B>");
	CHECK(PlaceholderText(layout.Clusters()[4]) == "\\xFE");
}


TEST_CASE("cluster boundaries and columns")
{
	const LineLayout layout("aあéb", LayoutOptions {});
	// a: 0..1 col 0, U+3042: 1..4 col 1-2, e+U+0301: 4..7 col 3, b: 7..8 col 4
	CHECK(layout.Width() == 5);

	CHECK(layout.IsBoundary(0));
	CHECK(layout.IsBoundary(1));
	CHECK_FALSE(layout.IsBoundary(2));
	CHECK(layout.IsBoundary(4));
	CHECK_FALSE(layout.IsBoundary(5));
	CHECK(layout.IsBoundary(8));
	CHECK_FALSE(layout.IsBoundary(9));

	CHECK(layout.NextBoundary(0) == 1);
	CHECK(layout.NextBoundary(1) == 4);
	CHECK(layout.NextBoundary(2) == 4);
	CHECK(layout.NextBoundary(4) == 7);
	CHECK(layout.NextBoundary(8) == 8);
	CHECK(layout.PreviousBoundary(8) == 7);
	CHECK(layout.PreviousBoundary(7) == 4);
	CHECK(layout.PreviousBoundary(6) == 4);
	CHECK(layout.PreviousBoundary(4) == 1);
	CHECK(layout.PreviousBoundary(0) == 0);

	CHECK(layout.ColumnOfOffset(0) == 0);
	CHECK(layout.ColumnOfOffset(1) == 1);
	CHECK(layout.ColumnOfOffset(3) == 1);
	CHECK(layout.ColumnOfOffset(4) == 3);
	CHECK(layout.ColumnOfOffset(7) == 4);
	CHECK(layout.ColumnOfOffset(8) == 5);

	CHECK(layout.ClusterAtOffset(2) == 1);
	CHECK(layout.ClusterAtOffset(8) == 4);
	CHECK(layout.ClusterAtColumn(0) == 0);
	CHECK(layout.ClusterAtColumn(2) == 1);
	CHECK(layout.ClusterAtColumn(3) == 2);
	CHECK(layout.ClusterAtColumn(5) == 4);

	CHECK(layout.BoundaryNearColumn(-1.0f) == 0);
	CHECK(layout.BoundaryNearColumn(0.4f) == 0);
	CHECK(layout.BoundaryNearColumn(0.6f) == 1);
	CHECK(layout.BoundaryNearColumn(1.9f) == 1);
	CHECK(layout.BoundaryNearColumn(2.1f) == 4);
	CHECK(layout.BoundaryNearColumn(3.6f) == 7);
	CHECK(layout.BoundaryNearColumn(100.0f) == 8);

	const LineLayout empty("", LayoutOptions {});
	CHECK(empty.ClusterAtOffset(0) == 0);
	CHECK(empty.BoundaryNearColumn(3.0f) == 0);
	CHECK(empty.ColumnOfOffset(0) == 0);
}


static std::vector<WrapRow> Wrap(std::string_view line, uint32_t width, uint32_t indent)
{
	return WrapLine(LineLayout(line, {}), width, indent);
}


TEST_CASE("wrapping at whitespace and punctuation")
{
	using Rows = std::vector<WrapRow>;
	CHECK(Wrap("hello world", 0, 4) == Rows {{0, 0, 0}});
	CHECK(Wrap("hello world", 11, 4) == Rows {{0, 0, 0}});
	CHECK(Wrap("", 4, 4) == Rows {{0, 0, 0}});

	CHECK(Wrap("hello world foo", 8, 4) == Rows {{0, 0, 0}, {6, 6, 4}, {10, 10, 4}, {12, 12, 4}});
	// Blanks at the break hang past the edge.
	CHECK(Wrap("ab    cd", 4, 4) == Rows {{0, 0, 0}, {6, 6, 2}});
	// Continuation indent follows the leading whitespace (a tab here).
	CHECK(Wrap("\tx = aaaa + bbbb;", 16, 4) == Rows {{0, 0, 0}, {12, 15, 8}});
	CHECK(Wrap("foo(bar,baz)", 6, 0) == Rows {{0, 0, 0}, {4, 4, 0}, {8, 8, 0}});
	// No break inside operators like "->".
	CHECK(Wrap("a b->c", 4, 0) == Rows {{0, 0, 0}, {2, 2, 0}});
}


TEST_CASE("wrapping wide characters and narrow widths")
{
	using Rows = std::vector<WrapRow>;
	CHECK(Wrap("あいうえお", 5, 0) == Rows {{0, 0, 0}, {6, 4, 0}, {12, 8, 0}});
	// Closing punctuation stays with the character before it.
	CHECK(Wrap("あい。う", 4, 0) == Rows {{0, 0, 0}, {3, 2, 0}, {9, 6, 0}});
	CHECK(Wrap("abc", 1, 4) == Rows {{0, 0, 0}, {1, 1, 0}, {2, 2, 0}});
	// A cluster wider than the row gets a row of its own.
	CHECK(Wrap("あa", 1, 0) == Rows {{0, 0, 0}, {3, 2, 0}});
}


TEST_CASE("line width without clusters")
{
	const std::string_view lines[] = {
		"",
		"plain ascii text",
		"\ttab\tafter\ttext",
		"あいう abc",
		"e\xcc\x81 combining",
		"1\xef\xb8\x8f\xe2\x83\xa3 keycap",
		"x\xef\xb8\x8f",
		"\x01\x7f control",
		"\xff\xfe invalid",
		"\xf0\x9f\x87\xaf\xf0\x9f\x87\xb5 flag",
		"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9 joined",
		"a\xe2\x80\x8d",
		"\xc2\xa7 ambiguous",
	};
	for (const uint32_t ambiguousWidth : {1, 2}) {
		LayoutOptions options;
		options.tabWidth = 3;
		options.ambiguousWidth = ambiguousWidth;
		for (const std::string_view line : lines) {
			CAPTURE(line);
			CHECK(LineWidth(line, options) == LineLayout(line, options).Width());
		}
	}
}


} // CodeEdit
