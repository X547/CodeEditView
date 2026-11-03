#pragma once

#include <stdint.h>


namespace CodeEdit {


// Cell layout class of a code point. Values are used by the generated table.
enum class CharClass: uint8_t {
	Narrow,
	Wide,
	Ambiguous,
	// Zero width, extends the preceding cluster.
	Combining,
	// Invisible format character, shown as a placeholder.
	Format,
	// Control character, shown as a placeholder.
	Control,
};


struct CharProperties {
	CharClass charClass {};
	bool emoji {};
	bool emojiPresentation {};
};


constexpr char32_t kZeroWidthJoiner = 0x200d;
constexpr char32_t kTextVariationSelector = 0xfe0e;
constexpr char32_t kEmojiVariationSelector = 0xfe0f;


CharProperties GetCharProperties(char32_t codePoint);
const char *UnicodeVersion();


inline bool IsRegionalIndicator(char32_t codePoint)
{
	return codePoint >= 0x1f1e6 && codePoint <= 0x1f1ff;
}


inline bool IsEmojiModifier(char32_t codePoint)
{
	return codePoint >= 0x1f3fb && codePoint <= 0x1f3ff;
}


} // CodeEdit
