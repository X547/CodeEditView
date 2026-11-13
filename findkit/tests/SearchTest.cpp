#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Search.h"


namespace FindKit {


static bool Is(const std::optional<Match> &match, size_t offset, size_t length)
{
	return match.has_value() && match->offset == offset && match->length == length;
}


static std::string Replace(const Searcher &searcher, std::string_view text, std::string_view replacement)
{
	const std::optional<Match> match = searcher.Find(text, 0);
	REQUIRE(match.has_value());
	return searcher.Expand(text, *match, replacement);
}


TEST_CASE("exact search matches bytes across lines")
{
	const std::string text = "a\nfoo\nbar\nfoo\nbar";
	const Searcher searcher("foo\nbar", false, false);
	CHECK(Is(searcher.Find(text, 0), 2, 7));
	CHECK(Is(searcher.Find(text, 3), 10, 7));
	CHECK_FALSE(searcher.Find(text, 11).has_value());
	CHECK_FALSE(Searcher("FOO", false, false).Find(text, 0).has_value());
}


TEST_CASE("exact search treats regex syntax literally")
{
	const Searcher searcher("a.*(b", false, false);
	CHECK(Is(searcher.Find("xa.*(b", 0), 1, 5));
	CHECK_FALSE(searcher.Find("axxb", 0).has_value());
	CHECK(searcher.Expand("a.*(b", *searcher.Find("a.*(b", 0), "$1\\n") == "$1\\n");
}


TEST_CASE("ignore case folds ASCII and Unicode")
{
	const Searcher exact("Straße ÄB", false, true);
	CHECK(Is(exact.Find("x straße äb", 0), 2, 11));
	const Searcher regex("ä+", true, true);
	CHECK(Is(regex.Find("xÄä", 0), 1, 4));
}


TEST_CASE("empty and invalid patterns throw")
{
	CHECK_THROWS_AS(Searcher("", false, false), PatternError);
	CHECK_THROWS_AS(Searcher("", true, false), PatternError);
	try {
		Searcher("ab(c", true, false);
		FAIL("no exception");
	} catch (const PatternError &error) {
		CHECK(error.Offset() == 4);
		CHECK(std::string(error.what()).size() > 0);
	}
}


TEST_CASE("regex sees the text before the start offset")
{
	const std::string text = "ab\nb";
	const Searcher lineStart("^b", true, false);
	CHECK(Is(lineStart.Find(text, 1), 3, 1));
	CHECK_FALSE(lineStart.MatchAt(text, 1).has_value());
	CHECK(Is(lineStart.MatchAt(text, 3), 3, 1));

	const Searcher lookbehind("(?<=a)b", true, false);
	CHECK(Is(lookbehind.Find(text, 1), 1, 1));
	CHECK(Is(lookbehind.MatchAt(text, 1), 1, 1));
}


TEST_CASE("regex matches multiple lines, dot stops at newlines")
{
	const std::string text = "int a;\nint b;\n";
	CHECK(Is(Searcher(";\\nint", true, false).Find(text, 0), 5, 5));
	CHECK(Is(Searcher("a.*", true, false).Find(text, 0), 4, 2));
	CHECK(Is(Searcher("b;$", true, false).Find(text, 0), 11, 2));
}


TEST_CASE("invalid UTF-8 in the text does not stop the search")
{
	const std::string text = "\xff\xfe" "foo";
	CHECK(Is(Searcher("foo", true, false).Find(text, 0), 2, 3));
	CHECK(Is(Searcher("FOO", false, true).Find(text, 0), 2, 3));
}


TEST_CASE("find last returns the last match before an offset")
{
	const std::string text = "abab";
	for (const bool regex: {false, true}) {
		const Searcher searcher("ab", regex, false);
		CHECK(Is(searcher.FindLast(text, 4), 2, 2));
		CHECK(Is(searcher.FindLast(text, 2), 0, 2));
		CHECK(Is(searcher.FindLast(text, 1), 0, 2));
		CHECK_FALSE(searcher.FindLast(text, 0).has_value());
		CHECK(Is(searcher.FindLast(text, 100), 2, 2));
	}
	CHECK(Is(Searcher("$", true, false).FindLast("a\nb", 4), 3, 0));
}


TEST_CASE("find all yields non-overlapping and empty matches like Perl")
{
	const std::vector<Match> matches = Searcher("x*", true, false).FindAll("axb", 0, 3);
	REQUIRE(matches.size() == 4);
	CHECK((matches[0].offset == 0 && matches[0].length == 0));
	CHECK((matches[1].offset == 1 && matches[1].length == 1));
	CHECK((matches[2].offset == 2 && matches[2].length == 0));
	CHECK((matches[3].offset == 3 && matches[3].length == 0));

	CHECK(Searcher("aa", false, false).FindAll("aaaaa", 0, 5).size() == 2);
}


TEST_CASE("find all keeps matches inside the range")
{
	const std::string text = "abab abab";
	const std::vector<Match> matches = Searcher("ab", false, false).FindAll(text, 1, 7);
	REQUIRE(matches.size() == 2);
	CHECK(matches[0].offset == 2);
	CHECK(matches[1].offset == 5);

	// A match crossing the end does not hide a later one inside.
	const std::vector<Match> later = Searcher("b.*d|c", true, false).FindAll("bcd", 0, 2);
	REQUIRE(later.size() == 1);
	CHECK((later[0].offset == 1 && later[0].length == 1));
}


TEST_CASE("regex replacement expands groups and escapes")
{
	const Searcher searcher("(\\w+)=(\\d+)", true, false);
	const std::string text = "x=12";
	CHECK(Replace(searcher, text, "$2:$1") == "12:x");
	CHECK(Replace(searcher, text, "\\2-\\1") == "12-x");
	CHECK(Replace(searcher, text, "${1}0") == "x0");
	CHECK(Replace(searcher, text, "$0") == "x=12");
	CHECK(Replace(searcher, text, "$$1 \\$1 \\\\") == "$1 $1 \\");
	CHECK(Replace(searcher, text, "a\\nb\\tc") == "a\nb\tc");
	CHECK(Replace(searcher, text, "[$9][${12}]") == "[][]");
	CHECK(Replace(searcher, text, "$10") == "x0");
	CHECK(Replace(searcher, text, "\\q ${x} $ ${") == "\\q ${x} $ ${");
	CHECK(Replace(Searcher("(a)|(b)", true, false), "b", "[$1][$2]") == "[][b]");
}


static bool Same(const std::optional<Match> &a, const std::optional<Match> &b)
{
	if (a.has_value() != b.has_value()) {
		return false;
	}
	return !a.has_value() || (a->offset == b->offset && a->length == b->length && a->groups == b->groups);
}


TEST_CASE("searching through small windows finds what one window finds")
{
	std::string text;
	for (int i = 0; i < 40; i++) {
		text += "int value" + std::to_string(i) + " = " + std::string(i % 7, 'a') + ";\n\xe3\x81\x82";
	}
	text += std::string(100, 'a') + "\nend";

	struct Pattern {
		const char *pattern;
		bool regex;
		bool ignoreCase;
	};
	const Pattern patterns[] = {
		{"value1", false, false},
		{"VALUE", false, true},
		{";\n\xe3\x81\x82int", false, false},
		{"a+", true, false},
		{"(?<=value)(\\d+) = (a*)", true, false},
		{"^int", true, false},
		{"\\d;$", true, false},
		{"\\bvalue\\d\\b", true, false},
		{"x*", true, false},
		{"a{3}(?=;)", true, false},
		{"=\\s+a+;\\n\\S", true, false},
	};
	for (const Pattern &pattern : patterns) {
		const Searcher searcher(pattern.pattern, pattern.regex, pattern.ignoreCase);
		const StringSource whole(text);
		for (const size_t windowSize : {16, 17, 23, 100}) {
			INFO("pattern " << pattern.pattern << ", window " << windowSize);
			const StringSource windows(text, windowSize);
			for (size_t from = 0; from <= text.size() + 1; from += 13) {
				CHECK(Same(searcher.Find(windows, from), searcher.Find(whole, from)));
				CHECK(Same(searcher.MatchAt(windows, from), searcher.MatchAt(whole, from)));
				CHECK(Same(searcher.FindLast(windows, from), searcher.FindLast(whole, from)));
			}
			const std::vector<Match> all = searcher.FindAll(whole, 0, text.size());
			const std::vector<Match> allWindows = searcher.FindAll(windows, 0, text.size());
			REQUIRE(all.size() == allWindows.size());
			for (size_t i = 0; i < all.size(); i++) {
				CHECK(Same(all[i], allWindows[i]));
			}
		}
	}
}


TEST_CASE("windows grow for long matches")
{
	const std::string text = "x" + std::string(5000, 'a') + "y";
	const StringSource windows(text, 16);
	CHECK(Is(Searcher("a+y", true, false).Find(windows, 0), 1, 5001));
	CHECK(Is(Searcher("xa+", true, false).MatchAt(windows, 0), 0, 5001));
	CHECK(Is(Searcher(std::string(3000, 'a') + "y", false, false).Find(windows, 0), 2001, 3001));
}


TEST_CASE("find last goes back through the text")
{
	std::string text = "match";
	text += std::string(100000, '-') + "\n";
	text += std::string(100000, '.');
	for (const bool regex : {false, true}) {
		const Searcher searcher("match", regex, false);
		const StringSource windows(text, 1000);
		CHECK(Is(searcher.FindLast(windows, text.size()), 0, 5));
		CHECK_FALSE(searcher.FindLast(windows, 0).has_value());
	}
	// Exact search finds overlapping occurrences.
	CHECK(Is(Searcher("aa", false, false).FindLast("aaa", 3), 1, 2));
}


TEST_CASE("replacement expands groups from the source")
{
	const std::string text = std::string(50, ' ') + "key=value";
	const Searcher searcher("(\\w+)=(\\w+)", true, false);
	const StringSource windows(text, 16);
	const std::optional<Match> match = searcher.Find(windows, 0);
	REQUIRE(match.has_value());
	CHECK(searcher.Expand(windows, *match, "$2=$1") == "value=key");
}


TEST_CASE("line endings are normalized to LF")
{
	CHECK(NormalizeLineEndings("a\r\nb\rc\n\r") == "a\nb\nc\n\n");
	CHECK(NormalizeLineEndings("") == "");
}


TEST_CASE("next code point skips continuation bytes")
{
	const std::string text = "aä€";
	CHECK(NextCodePoint(text, 0) == 1);
	CHECK(NextCodePoint(text, 1) == 3);
	CHECK(NextCodePoint(text, 3) == 6);
	CHECK(NextCodePoint(text, 6) == 6);
}


} // FindKit
