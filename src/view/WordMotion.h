#pragma once

#include <stddef.h>
#include <stdint.h>


namespace CodeEdit {


class Rope;


enum class WordClass: uint8_t {
	None,
	Underscore,
	Latin,
	Han,
	Hiragana,
	Katakana,
	Hangul,
};


enum class WordDirection {
	Backward,
	Forward,
};


WordClass ClassifyForWords(char32_t codePoint);

// Pe-style word movement: skips non-word characters (unless skipNonWord is
// false), then moves over characters of the same word class. Sub-word movement
// additionally stops at underscores and lower/upper case transitions.
size_t FindWordBoundary(
	const Rope &text,
	size_t offset,
	WordDirection direction,
	bool subword,
	bool skipNonWord = true
);


} // CodeEdit
