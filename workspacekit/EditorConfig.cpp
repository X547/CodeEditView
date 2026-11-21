#include "EditorConfig.h"

#include <stdlib.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>


namespace Workspace {


// Larger files are not read.
static constexpr std::streamsize kMaxFileSize = 1024 * 1024;
// Larger widths are taken for mistakes.
static constexpr unsigned long kMaxWidth = 64;


static std::string_view Trim(std::string_view text)
{
	const size_t start = text.find_first_not_of(" \t\r");
	if (start == std::string_view::npos) {
		return {};
	}
	return text.substr(start, text.find_last_not_of(" \t\r") - start + 1);
}


static std::string Lower(std::string_view text)
{
	std::string result(text);
	std::transform(result.begin(), result.end(), result.begin(), [](char ch) {
		return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
	});
	return result;
}


//#pragma mark - Globs

static bool Match(std::string_view glob, std::string_view text);


// Index of the brace closing the one at open; npos if there is none.
static size_t ClosingBrace(std::string_view glob, size_t open)
{
	int depth = 0;
	for (size_t i = open; i < glob.size(); i++) {
		if (glob[i] == '\\') {
			i++;
		} else if (glob[i] == '{') {
			depth++;
		} else if (glob[i] == '}' && --depth == 0) {
			return i;
		}
	}
	return std::string_view::npos;
}


// The alternatives of a brace's contents, split at commas outside of
// nested braces.
static std::vector<std::string_view> Alternatives(std::string_view body)
{
	std::vector<std::string_view> result;
	int depth = 0;
	size_t start = 0;
	for (size_t i = 0; i < body.size(); i++) {
		if (body[i] == '\\') {
			i++;
		} else if (body[i] == '{') {
			depth++;
		} else if (body[i] == '}') {
			depth--;
		} else if (body[i] == ',' && depth == 0) {
			result.push_back(body.substr(start, i - start));
			start = i + 1;
		}
	}
	result.push_back(body.substr(start));
	return result;
}


static bool ParseInteger(std::string_view text, long &value)
{
	if (text.empty()) {
		return false;
	}
	const std::string string(text);
	char *end = nullptr;
	value = strtol(string.c_str(), &end, 10);
	return *end == '\0';
}


// "{num1..num2}".
static bool ParseRange(std::string_view body, long &from, long &to)
{
	const size_t dots = body.find("..");
	return dots != std::string_view::npos && ParseInteger(body.substr(0, dots), from)
		&& ParseInteger(body.substr(dots + 2), to);
}


static bool MatchBrace(std::string_view glob, size_t close, std::string_view text)
{
	const std::string_view body = glob.substr(1, close - 1);
	const std::string_view rest = glob.substr(close + 1);
	long from = 0;
	long to = 0;
	if (ParseRange(body, from, to)) {
		size_t length = text.starts_with('-') ? 1 : 0;
		while (length < text.size() && text[length] >= '0' && text[length] <= '9') {
			length++;
		}
		long value = 0;
		return ParseInteger(text.substr(0, length), value) && value >= std::min(from, to)
			&& value <= std::max(from, to) && Match(rest, text.substr(length));
	}
	const std::vector<std::string_view> alternatives = Alternatives(body);
	if (alternatives.size() == 1) {
		// A single choice is no choice: the braces are literal.
		return text.starts_with('{') && Match(glob.substr(1), text.substr(1));
	}
	for (const std::string_view alternative : alternatives) {
		std::string expanded(alternative);
		expanded.append(rest);
		if (Match(expanded, text)) {
			return true;
		}
	}
	return false;
}


// "[seq]" or "[!seq]" at the start of glob; false if it is not a class.
static bool MatchClass(std::string_view glob, std::string_view text, bool &matched)
{
	size_t i = 1;
	const bool negated = i < glob.size() && glob[i] == '!';
	if (negated) {
		i++;
	}
	const size_t first = i;
	while (i < glob.size() && (glob[i] != ']' || i == first)) {
		i++;
	}
	if (i >= glob.size()) {
		return false;
	}
	const std::string_view set = glob.substr(first, i - first);
	if (text.empty() || text[0] == '/') {
		matched = false;
		return true;
	}
	bool inSet = false;
	for (size_t j = 0; j < set.size(); j++) {
		if (j + 2 < set.size() && set[j + 1] == '-') {
			inSet = inSet || (text[0] >= set[j] && text[0] <= set[j + 2]);
			j += 2;
		} else {
			inSet = inSet || text[0] == set[j];
		}
	}
	matched = inSet != negated && Match(glob.substr(i + 1), text.substr(1));
	return true;
}


static bool Match(std::string_view glob, std::string_view text)
{
	if (glob.empty()) {
		return text.empty();
	}
	switch (glob[0]) {
		case '\\':
			if (glob.size() > 1) {
				return !text.empty() && text[0] == glob[1] && Match(glob.substr(2), text.substr(1));
			}
			break;
		case '*': {
			if (glob.starts_with("**")) {
				const std::string_view rest = glob.substr(2);
				for (size_t i = 0; i <= text.size(); i++) {
					if (Match(rest, text.substr(i))) {
						return true;
					}
				}
				return false;
			}
			const std::string_view rest = glob.substr(1);
			for (size_t i = 0; i <= text.size(); i++) {
				if (Match(rest, text.substr(i))) {
					return true;
				}
				if (i < text.size() && text[i] == '/') {
					break;
				}
			}
			return false;
		}
		case '?':
			return !text.empty() && text[0] != '/' && Match(glob.substr(1), text.substr(1));
		case '[': {
			bool matched = false;
			if (MatchClass(glob, text, matched)) {
				return matched;
			}
			break;
		}
		case '{': {
			const size_t close = ClosingBrace(glob, 0);
			if (close != std::string_view::npos) {
				return MatchBrace(glob, close, text);
			}
			break;
		}
		default:
			break;
	}
	return !text.empty() && text[0] == glob[0] && Match(glob.substr(1), text.substr(1));
}


bool MatchEditorConfigGlob(std::string_view glob, std::string_view relativePath)
{
	if (glob.find('/') == std::string_view::npos) {
		const size_t slash = relativePath.rfind('/');
		return Match(glob, slash == std::string_view::npos ? relativePath : relativePath.substr(slash + 1));
	}
	if (glob.starts_with('/')) {
		glob.remove_prefix(1);
	}
	return Match(glob, relativePath);
}


//#pragma mark - Files

bool ApplyEditorConfig(std::string_view text, std::string_view relativePath, EditorConfigProperties &properties)
{
	bool root = false;
	bool inSection = false;
	bool matches = false;
	while (!text.empty()) {
		const size_t end = text.find('\n');
		const std::string_view line = Trim(text.substr(0, end));
		text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
		if (line.empty() || line[0] == '#' || line[0] == ';') {
			continue;
		}
		if (line[0] == '[' && line.back() == ']') {
			inSection = true;
			matches = MatchEditorConfigGlob(line.substr(1, line.size() - 2), relativePath);
			continue;
		}
		const size_t equal = line.find('=');
		if (equal == std::string_view::npos) {
			continue;
		}
		const std::string key = Lower(Trim(line.substr(0, equal)));
		const std::string value = Lower(Trim(line.substr(equal + 1)));
		if (!inSection) {
			root = root || (key == "root" && value == "true");
		} else if (matches) {
			if (value == "unset") {
				properties.erase(key);
			} else {
				properties[key] = value;
			}
		}
	}
	return root;
}


static std::optional<uint32_t> Width(const EditorConfigProperties &properties, const char *key)
{
	const auto it = properties.find(key);
	if (it == properties.end()) {
		return std::nullopt;
	}
	long value = 0;
	if (!ParseInteger(it->second, value) || value < 1 || static_cast<unsigned long>(value) > kMaxWidth) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(value);
}


EditorConfig ResolveEditorConfig(const EditorConfigProperties &properties)
{
	EditorConfig config;
	if (const auto style = properties.find("indent_style"); style != properties.end()) {
		if (style->second == "tab") {
			config.insertSpaces = false;
		} else if (style->second == "space") {
			config.insertSpaces = true;
		}
	}
	const auto indentSize = properties.find("indent_size");
	if (indentSize != properties.end() && indentSize->second == "tab") {
		config.indentSize = 0;
	} else {
		config.indentSize = Width(properties, "indent_size");
	}
	config.tabWidth = Width(properties, "tab_width");
	if (!config.tabWidth.has_value() && config.indentSize.has_value() && *config.indentSize > 0) {
		config.tabWidth = config.indentSize;
	}
	return config;
}


static std::optional<std::string> ReadText(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return std::nullopt;
	}
	std::string text(kMaxFileSize, '\0');
	file.read(text.data(), kMaxFileSize);
	text.resize(file.gcount());
	return text;
}


EditorConfig ReadEditorConfig(const std::string &path)
{
	// Nearest first, up to a root file.
	std::vector<std::pair<std::string, std::string>> files;
	std::string directory = path.substr(0, path.rfind('/'));
	for (;;) {
		if (const std::optional<std::string> text = ReadText(directory + "/.editorconfig")) {
			EditorConfigProperties ignored;
			files.emplace_back(directory, *text);
			if (ApplyEditorConfig(*text, {}, ignored)) {
				break;
			}
		}
		if (directory.empty()) {
			break;
		}
		directory.resize(directory.rfind('/'));
	}

	EditorConfigProperties properties;
	for (auto file = files.rbegin(); file != files.rend(); ++file) {
		ApplyEditorConfig(file->second, std::string_view(path).substr(file->first.size() + 1), properties);
	}
	return ResolveEditorConfig(properties);
}


} // Workspace
