#include "WordMotion.h"

#include <algorithm>
#include <string_view>

#include <CodeEditView/Rope.h>

#include "unicode/CharWidth.h"
#include "unicode/Utf8.h"


namespace CodeEdit {


static bool InRange(char32_t codePoint, char32_t first, char32_t last)
{
	return codePoint >= first && codePoint <= last;
}


WordClass ClassifyForWords(char32_t codePoint)
{
	if (codePoint < 0x80) {
		if (codePoint == '_') {
			return WordClass::Underscore;
		}
		const bool isAlnum = InRange(codePoint, '0', '9') || InRange(codePoint, 'a', 'z')
			|| InRange(codePoint, 'A', 'Z');
		return isAlnum ? WordClass::Latin : WordClass::None;
	}
	if (codePoint < 0xc0) {
		const bool isLetter = codePoint == 0xaa || codePoint == 0xb5 || codePoint == 0xba;
		return isLetter ? WordClass::Latin : WordClass::None;
	}
	if (codePoint == 0xd7 || codePoint == 0xf7) {
		return WordClass::None;
	}
	if (InRange(codePoint, 0x2000, 0x2bff) || InRange(codePoint, 0x2e00, 0x2e7f)) {
		return WordClass::None;
	}
	if (InRange(codePoint, 0x3000, 0x303f)) {
		return InRange(codePoint, 0x3005, 0x3007) ? WordClass::Han : WordClass::None;
	}
	if (InRange(codePoint, 0x3040, 0x309f)) {
		return WordClass::Hiragana;
	}
	if (InRange(codePoint, 0x30a0, 0x30ff) || InRange(codePoint, 0x31f0, 0x31ff) || InRange(codePoint, 0xff66, 0xff9f)) {
		return codePoint == 0x30fb ? WordClass::None : WordClass::Katakana;
	}
	if (InRange(codePoint, 0x1100, 0x11ff) || InRange(codePoint, 0x3130, 0x318f) || InRange(codePoint, 0xac00, 0xd7af)) {
		return WordClass::Hangul;
	}
	if (InRange(codePoint, 0x3400, 0x4dbf) || InRange(codePoint, 0x4e00, 0x9fff) || InRange(codePoint, 0xf900, 0xfaff)
		|| InRange(codePoint, 0x20000, 0x3ffff)) {
		return WordClass::Han;
	}
	if (InRange(codePoint, 0xff10, 0xff19) || InRange(codePoint, 0xff21, 0xff3a) || InRange(codePoint, 0xff41, 0xff5a)) {
		return WordClass::Latin;
	}
	if (InRange(codePoint, 0xff00, 0xff65) || InRange(codePoint, 0xfe30, 0xfe6f) || InRange(codePoint, 0xe000, 0xf8ff)
		|| InRange(codePoint, 0x1f000, 0x1faff)) {
		return WordClass::None;
	}
	return WordClass::Latin;
}


static Utf8Char DecodeAt(const Rope &text, size_t offset)
{
	char bytes[4];
	const size_t count = std::min<size_t>(4, text.Length() - offset);
	for (size_t i = 0; i < count; i++) {
		bytes[i] = text.ByteAt(offset + i);
	}
	return DecodeUtf8(std::string_view(bytes, count), 0);
}


static size_t PreviousCodePointStart(const Rope &text, size_t offset)
{
	size_t start = offset - 1;
	while (start > 0 && offset - start < 4 && (static_cast<uint8_t>(text.ByteAt(start)) & 0xc0) == 0x80) {
		start--;
	}
	const Utf8Char ch = DecodeAt(text, start);
	if (ch.valid && start + ch.length == offset) {
		return start;
	}
	return offset - 1;
}


// Tracks the state of one word scan, mirroring Pe's WordState.
class WordScanner {
private:
	WordDirection fDirection;
	bool fSubword;
	bool fSkipping;
	bool fStarted {};
	WordClass fClass = WordClass::None;
	bool fLastIsUpper {};

	bool IsWordClass(WordClass wordClass) const
	{
		if (wordClass == WordClass::Underscore) {
			return !fSubword;
		}
		return wordClass != WordClass::None;
	}

public:
	WordScanner(WordDirection direction, bool subword, bool skipNonWord):
		fDirection(direction),
		fSubword(subword),
		fSkipping(skipNonWord)
	{
	}

	bool BelongsToWord(char32_t codePoint)
	{
		// Combining marks belong to whatever precedes them.
		if (GetCharProperties(codePoint).charClass == CharClass::Combining) {
			return true;
		}

		WordClass wordClass = ClassifyForWords(codePoint);
		if (wordClass == WordClass::Underscore && !fSubword) {
			wordClass = WordClass::Latin;
		}
		const bool isUpper = InRange(codePoint, 'A', 'Z');

		if (!fStarted) {
			if (!IsWordClass(wordClass)) {
				return fSkipping;
			}
			fStarted = true;
			fClass = wordClass;
			fLastIsUpper = isUpper;
			return true;
		}

		if (!IsWordClass(wordClass) || wordClass != fClass) {
			return false;
		}
		if (fSubword && codePoint < 0x80) {
			const bool forwardBreak = fDirection == WordDirection::Forward && isUpper && !fLastIsUpper;
			const bool backwardBreak = fDirection == WordDirection::Backward && !isUpper && fLastIsUpper;
			if (forwardBreak || backwardBreak) {
				return false;
			}
			fLastIsUpper = isUpper;
		}
		return true;
	}
};


// Invalid bytes are never part of a word.
static char32_t WordCodePoint(const Utf8Char &ch)
{
	return ch.valid ? ch.codePoint : 0;
}


size_t FindWordBoundary(const Rope &text, size_t offset, WordDirection direction, bool subword, bool skipNonWord)
{
	WordScanner scanner(direction, subword, skipNonWord);
	if (direction == WordDirection::Forward) {
		while (offset < text.Length()) {
			const Utf8Char ch = DecodeAt(text, offset);
			if (!scanner.BelongsToWord(WordCodePoint(ch))) {
				break;
			}
			offset += ch.length;
		}
		return offset;
	}

	while (offset > 0) {
		const size_t start = PreviousCodePointStart(text, offset);
		const Utf8Char ch = DecodeAt(text, start);
		if (!scanner.BelongsToWord(WordCodePoint(ch))) {
			break;
		}
		offset = start;
	}
	return offset;
}


} // CodeEdit
