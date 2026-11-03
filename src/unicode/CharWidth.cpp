#include "CharWidth.h"

#include <algorithm>
#include <iterator>


namespace CodeEdit {


struct CharWidthRange {
	uint32_t first;
	uint8_t info;
};


#include "CharWidthTable.inc"


static constexpr uint8_t kClassMask = 0x0f;
static constexpr uint8_t kFlagEmoji = 0x10;
static constexpr uint8_t kFlagEmojiPresentation = 0x20;
static constexpr char32_t kMaxCodePoint = 0x10ffff;


CharProperties GetCharProperties(char32_t codePoint)
{
	if (codePoint > kMaxCodePoint) {
		return {};
	}
	const auto next = std::upper_bound(
		std::begin(kCharWidthRanges),
		std::end(kCharWidthRanges),
		codePoint,
		[](char32_t value, const CharWidthRange &range) {
			return value < range.first;
		}
	);
	const uint8_t info = std::prev(next)->info;
	return {
		static_cast<CharClass>(info & kClassMask),
		(info & kFlagEmoji) != 0,
		(info & kFlagEmojiPresentation) != 0
	};
}


const char *UnicodeVersion()
{
	return kUnicodeVersion;
}


} // CodeEdit
