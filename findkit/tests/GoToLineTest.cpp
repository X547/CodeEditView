#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "GoToLinePanel.h"


namespace FindKit {


static bool Is(const std::optional<CodeEdit::TextPoint> &location, size_t line, size_t column)
{
	return location.has_value() && location->line == line && location->column == column;
}


TEST_CASE("Go to line locations", "[findkit]")
{
	CHECK(Is(ParseLocation("12"), 11, 0));
	CHECK(Is(ParseLocation(" 12:7 "), 11, 6));
	CHECK(Is(ParseLocation("1:1"), 0, 0));
	CHECK_FALSE(ParseLocation("").has_value());
	CHECK_FALSE(ParseLocation("0").has_value());
	CHECK_FALSE(ParseLocation("12:").has_value());
	CHECK_FALSE(ParseLocation("12:0").has_value());
	CHECK_FALSE(ParseLocation("a").has_value());
	CHECK_FALSE(ParseLocation("-3").has_value());
}


} // FindKit
