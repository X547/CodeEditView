#include "CommitMessage.h"

#include <ctype.h>


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


// "Token: value", where the token has letters, digits and dashes.
static bool IsTrailer(std::string_view line)
{
	size_t i = 0;
	while (i < line.size() && (isalnum(static_cast<unsigned char>(line[i])) || line[i] == '-')) {
		i++;
	}
	return i > 0 && i < line.size() && line[i] == ':';
}


static bool StartsWithToken(std::string_view line, std::string_view token)
{
	if (line.size() <= token.size() || line[token.size()] != ':') {
		return false;
	}
	for (size_t i = 0; i < token.size(); i++) {
		if (tolower(static_cast<unsigned char>(line[i])) != tolower(static_cast<unsigned char>(token[i]))) {
			return false;
		}
	}
	return true;
}


std::string AddChangeId(std::string_view message, std::string_view changeId)
{
	std::vector<std::string_view> lines;
	size_t lineStart = 0;
	while (lineStart < message.size()) {
		size_t lineEnd = message.find('\n', lineStart);
		if (lineEnd == std::string_view::npos) {
			lineEnd = message.size();
		}
		lines.push_back(message.substr(lineStart, lineEnd - lineStart));
		lineStart = lineEnd + 1;
	}
	while (!lines.empty() && lines.back().empty()) {
		lines.pop_back();
	}
	if (lines.empty() || lines[0].starts_with("fixup! ") || lines[0].starts_with("squash! ")) {
		return std::string(message);
	}

	// The trailer block is the last paragraph, if it is not the subject and
	// has only trailers and their indented continuation lines.
	size_t blockStart = lines.size();
	while (blockStart > 0 && !lines[blockStart - 1].empty()) {
		blockStart--;
	}
	bool hasBlock = blockStart > 0 && IsTrailer(lines[blockStart]);
	for (size_t i = blockStart; hasBlock && i < lines.size(); i++) {
		const bool continuation = i > blockStart && (lines[i][0] == ' ' || lines[i][0] == '\t');
		hasBlock = continuation || IsTrailer(lines[i]);
	}

	size_t insertAt = lines.size();
	if (hasBlock) {
		for (size_t i = blockStart; i < lines.size(); i++) {
			if (StartsWithToken(lines[i], "Change-Id")) {
				return std::string(message);
			}
			if (insertAt == lines.size() && StartsWithToken(lines[i], "Signed-off-by")) {
				insertAt = i;
			}
		}
	}

	const std::string changeIdLine = "Change-Id: " + std::string(changeId);
	std::string result;
	for (size_t i = 0; i < lines.size(); i++) {
		if (i == insertAt) {
			result += changeIdLine + "\n";
		}
		result += lines[i];
		result += '\n';
	}
	if (insertAt == lines.size()) {
		if (!hasBlock) {
			result += '\n';
		}
		result += changeIdLine + "\n";
	}
	return result;
}


} // GitDiff
