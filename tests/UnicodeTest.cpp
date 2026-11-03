#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "unicode/CharWidth.h"
#include "unicode/Utf8.h"


namespace CodeEdit {


static void CheckDecode(std::string_view text, char32_t codePoint, uint8_t length, bool valid)
{
	CAPTURE(text);
	const Utf8Char ch = DecodeUtf8(text, 0);
	CHECK(ch.codePoint == codePoint);
	CHECK(ch.length == length);
	CHECK(ch.valid == valid);
}


TEST_CASE("UTF-8 decoding")
{
	CheckDecode("a", 'a', 1, true);
	CheckDecode("\xc3\xa9", 0xe9, 2, true);
	CheckDecode("\xe3\x81\x82", 0x3042, 3, true);
	CheckDecode("\xf0\x9f\x98\x80", 0x1f600, 4, true);

	CheckDecode("\xc0\x80", 0xc0, 1, false);
	CheckDecode("\xe0\x80\x80", 0xe0, 1, false);
	CheckDecode("\xed\xa0\x80", 0xed, 1, false);
	CheckDecode("\xf4\x90\x80\x80", 0xf4, 1, false);
	CheckDecode("\xe3\x81", 0xe3, 1, false);
	CheckDecode("\x80", 0x80, 1, false);
	CheckDecode("\xf8\x88\x80\x80\x80", 0xf8, 1, false);
	CheckDecode("\xc3" "a", 0xc3, 1, false);

	const std::string_view text = "a\xe3\x81\x82";
	CHECK(DecodeUtf8(text, 1).codePoint == 0x3042);
}


static void CheckClass(char32_t codePoint, CharClass charClass)
{
	CAPTURE(static_cast<uint32_t>(codePoint));
	CHECK(GetCharProperties(codePoint).charClass == charClass);
}


TEST_CASE("character width classes")
{
	CHECK(std::string_view(UnicodeVersion()).size() > 0);

	CheckClass('a', CharClass::Narrow);
	CheckClass(0x3042, CharClass::Wide);
	CheckClass(0x4e00, CharClass::Wide);
	CheckClass(0x20000, CharClass::Wide);
	CheckClass(0x2fffd, CharClass::Wide);
	CheckClass(0xff21, CharClass::Wide);
	CheckClass(0xff71, CharClass::Narrow);
	CheckClass(0xe9, CharClass::Ambiguous);
	CheckClass(0x0301, CharClass::Combining);
	CheckClass(0x1160, CharClass::Combining);
	CheckClass(kZeroWidthJoiner, CharClass::Combining);
	CheckClass(kEmojiVariationSelector, CharClass::Combining);
	CheckClass(0xe0067, CharClass::Combining);
	CheckClass(0x200b, CharClass::Format);
	CheckClass(0xe0001, CharClass::Format);
	CheckClass(0x2028, CharClass::Format);
	CheckClass(0x07, CharClass::Control);
	CheckClass(0x85, CharClass::Control);
	CheckClass(0x110000, CharClass::Narrow);

	const CharProperties grinning = GetCharProperties(0x1f600);
	CHECK(grinning.charClass == CharClass::Wide);
	CHECK(grinning.emoji);
	CHECK(grinning.emojiPresentation);

	const CharProperties heart = GetCharProperties(0x2764);
	CHECK(heart.charClass == CharClass::Narrow);
	CHECK(heart.emoji);
	CHECK_FALSE(heart.emojiPresentation);

	CHECK(GetCharProperties('#').emoji);
	CHECK_FALSE(GetCharProperties('a').emoji);
}


} // CodeEdit
