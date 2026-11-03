#include <CodeEditView/Theme.h>

#include <InterfaceDefs.h>


namespace CodeEdit {


static constexpr rgb_color Rgb(uint8 red, uint8 green, uint8 blue)
{
	return {red, green, blue, 255};
}


// Colors from Pe's defaults (PApp.cpp). Pe colors language keywords, built-in
// types, preprocessor directives and NULL with its keyword color, and library
// types with "user set 1".
Theme Theme::Pe()
{
	Theme theme;
	auto set = [&theme](HighlightStyle style, rgb_color color) {
		theme.colors[static_cast<size_t>(style)] = color;
	};
	const rgb_color keyword = Rgb(0x39, 0x74, 0x79);
	const rgb_color userSet = Rgb(0x44, 0x8a, 0x00);
	const rgb_color number = Rgb(0x85, 0x19, 0x19);

	set(HighlightStyle::Default, Rgb(0x00, 0x00, 0x00));
	set(HighlightStyle::Comment, Rgb(0xa1, 0x64, 0x0e));
	set(HighlightStyle::String, Rgb(0x3f, 0x48, 0x84));
	set(HighlightStyle::Character, number);
	set(HighlightStyle::Number, number);
	set(HighlightStyle::Keyword, keyword);
	set(HighlightStyle::Preprocessor, keyword);
	set(HighlightStyle::Type, userSet);
	set(HighlightStyle::BuiltinType, keyword);
	set(HighlightStyle::Constant, keyword);
	set(HighlightStyle::Function, Rgb(0x00, 0x00, 0x00));
	set(HighlightStyle::Operator, userSet);
	set(HighlightStyle::Punctuation, userSet);
	set(HighlightStyle::Error, Rgb(0xff, 0x00, 0x00));
	return theme;
}


rgb_color Theme::Color(HighlightStyle style) const
{
	if (style == HighlightStyle::Default && useSystemTextColor) {
		return ui_color(B_DOCUMENT_TEXT_COLOR);
	}
	return colors[static_cast<size_t>(style)];
}


HighlightStyle StyleForCapture(std::string_view name)
{
	struct CaptureStyle {
		std::string_view name;
		HighlightStyle style;
	};
	static constexpr CaptureStyle kCaptureStyles[] = {
		{"comment",          HighlightStyle::Comment},
		{"string",           HighlightStyle::String},
		{"character",        HighlightStyle::Character},
		{"number",           HighlightStyle::Number},
		{"keyword",          HighlightStyle::Keyword},
		{"preprocessor",     HighlightStyle::Preprocessor},
		{"type",             HighlightStyle::Type},
		{"type.builtin",     HighlightStyle::BuiltinType},
		{"constant",         HighlightStyle::Constant},
		{"function",         HighlightStyle::Function},
		{"operator",         HighlightStyle::Operator},
		{"punctuation",      HighlightStyle::Punctuation},
		{"error",            HighlightStyle::Error},
	};

	for (;;) {
		for (const CaptureStyle &entry : kCaptureStyles) {
			if (entry.name == name) {
				return entry.style;
			}
		}
		const size_t dot = name.rfind('.');
		if (dot == std::string_view::npos) {
			return HighlightStyle::Default;
		}
		name = name.substr(0, dot);
	}
}


} // CodeEdit
