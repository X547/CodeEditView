#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string_view>


namespace CodeEdit {


struct Utf8Char {
	// For an invalid sequence: the value of the first byte.
	char32_t codePoint {};
	uint8_t length {};
	bool valid {};
};


// Strictly decodes the sequence at offset (< text.size()). Overlong forms,
// surrogates, truncated sequences and stray continuation bytes are invalid and
// consume one byte.
Utf8Char DecodeUtf8(std::string_view text, size_t offset);


} // CodeEdit
