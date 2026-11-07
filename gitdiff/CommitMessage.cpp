#include "CommitMessage.h"


namespace GitDiff {


using CodeEdit::Decoration;
using CodeEdit::DecorationKind;


static constexpr rgb_color kSubjectColor = {255, 160, 0, 90};
static constexpr rgb_color kErrorColor = {230, 40, 40, 70};
static constexpr uint32_t kTabWidth = 8;


CodeEdit::EditorSettings CommitMessageSettings()
{
	CodeEdit::EditorSettings settings;
	settings.wordWrap = true;
	settings.wrapIndent = 0;
	settings.marginColumn = kBodyLimit;
	settings.hardWrapColumn = kBodyLimit;
	settings.hardWrapFirstLine = 1;
	settings.tabWidth = kTabWidth;
	return settings;
}


// The byte offset in line where the given column starts; the line length if
// it is shorter. Columns count code points, tabs to the next tab stop.
static size_t OffsetOfColumn(std::string_view line, uint32_t limit)
{
	uint32_t column = 0;
	for (size_t i = 0; i < line.size(); i++) {
		const unsigned char byte = static_cast<unsigned char>(line[i]);
		if ((byte & 0xc0) == 0x80) {
			continue;
		}
		if (column >= limit) {
			return i;
		}
		column = byte == '\t' ? (column / kTabWidth + 1) * kTabWidth : column + 1;
	}
	return line.size();
}


std::vector<Decoration> CommitMessageDecorations(std::string_view text)
{
	std::vector<Decoration> decorations;
	size_t lineStart = 0;
	for (size_t line = 0; lineStart <= text.size(); line++) {
		size_t lineEnd = text.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) {
			lineEnd = text.size();
		}
		const std::string_view lineText = text.substr(lineStart, lineEnd - lineStart);
		if (line == 1) {
			if (!lineText.empty()) {
				decorations.push_back({DecorationKind::LineBackground, lineStart, lineEnd, kErrorColor});
			}
		} else {
			const uint32_t limit = line == 0 ? kSubjectLimit : kBodyLimit;
			const size_t over = OffsetOfColumn(lineText, limit);
			if (over < lineText.size()) {
				decorations.push_back({DecorationKind::RangeBackground, lineStart + over, lineEnd,
					line == 0 ? kSubjectColor : kErrorColor});
			}
		}
		lineStart = lineEnd + 1;
	}
	return decorations;
}


} // GitDiff
