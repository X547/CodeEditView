#pragma once

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>


namespace Workspace {


// Settings of one file chosen by the user, kept with the file (an attribute)
// where the provider has attributes.
struct FileOptions {
	// A language of CodeEdit::AvailableLanguages(), or empty for plain text;
	// none: from the MIME type or the path.
	std::optional<std::string> language;
	// The indentation; none: from an .editorconfig file, else the default
	// (Indentation).
	std::optional<uint32_t> tabWidth;
	// Width of one indentation level with spaces; 0: the tab width.
	std::optional<uint32_t> indentSize;
	std::optional<bool> insertSpaces;
	bool wordWrap {};

	bool operator==(const FileOptions &other) const = default;
};


// The indentation a file gets.
struct Indentation {
	uint32_t tabWidth = 4;
	// 0: the tab width.
	uint32_t indentSize = 0;
	bool insertSpaces {};

	bool operator==(const Indentation &other) const = default;
};


// The attribute holding the options: a flattened BMessage.
constexpr const char *kFileOptionsAttribute = "CodeEdit:options";
// The attribute holding the lines of the bookmarks (0-based) as they were
// when the file was last saved: a flattened BMessage.
constexpr const char *kBookmarksAttribute = "CodeEdit:bookmarks";

// The attributes' data; options with missing or invalid fields keep their
// defaults, data that is no message gives none.
std::string FlattenFileOptions(const FileOptions &options);
std::optional<FileOptions> UnflattenFileOptions(std::string_view data);
std::string FlattenBookmarks(const std::vector<size_t> &lines);
std::vector<size_t> UnflattenBookmarks(std::string_view data);


} // Workspace
