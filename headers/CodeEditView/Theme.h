#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string_view>

#include <GraphicsDefs.h>


namespace CodeEdit {


enum class HighlightStyle: uint8_t {
	Default,
	Comment,
	String,
	Character,
	Number,
	Keyword,
	Preprocessor,
	Type,
	BuiltinType,
	Constant,
	Function,
	Operator,
	Punctuation,
	Error,
	Count,
};


constexpr size_t kHighlightStyleCount = static_cast<size_t>(HighlightStyle::Count);


struct Theme {
	rgb_color colors[kHighlightStyleCount] {};
	// Draw HighlightStyle::Default with the system document text color.
	bool useSystemTextColor = true;

	// Pe's default syntax coloring.
	static Theme Pe();

	rgb_color Color(HighlightStyle style) const;
};


// Style for a highlight query capture name such as "type.builtin"; unknown
// names fall back to their prefix ("type"), then to Default.
HighlightStyle StyleForCapture(std::string_view name);


} // CodeEdit
