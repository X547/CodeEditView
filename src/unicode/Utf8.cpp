#include "Utf8.h"


namespace CodeEdit {


Utf8Char DecodeUtf8(std::string_view text, size_t offset)
{
	const uint8_t lead = static_cast<uint8_t>(text[offset]);
	if (lead < 0x80) {
		return {lead, 1, true};
	}

	const Utf8Char invalid {lead, 1, false};
	uint8_t length;
	char32_t codePoint;
	char32_t minCodePoint;
	if ((lead & 0xe0) == 0xc0) {
		length = 2;
		codePoint = lead & 0x1f;
		minCodePoint = 0x80;
	} else if ((lead & 0xf0) == 0xe0) {
		length = 3;
		codePoint = lead & 0x0f;
		minCodePoint = 0x800;
	} else if ((lead & 0xf8) == 0xf0) {
		length = 4;
		codePoint = lead & 0x07;
		minCodePoint = 0x10000;
	} else {
		return invalid;
	}

	if (text.size() - offset < length) {
		return invalid;
	}
	for (size_t i = 1; i < length; i++) {
		const uint8_t byte = static_cast<uint8_t>(text[offset + i]);
		if ((byte & 0xc0) != 0x80) {
			return invalid;
		}
		codePoint = (codePoint << 6) | (byte & 0x3f);
	}
	if (codePoint < minCodePoint || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
		return invalid;
	}
	return {codePoint, length, true};
}


} // CodeEdit
