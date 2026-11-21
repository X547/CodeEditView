#pragma once

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>


namespace Workspace {


// Settings of one file chosen by the user, kept with the file (an attribute)
// where the provider has attributes.
struct FileOptions {
	// A language of CodeEdit::AvailableLanguages(), or empty for plain text;
	// none: from the MIME type or the path.
	std::optional<std::string> language;
	uint32_t tabWidth = 4;
	// Width of one indentation level with spaces; 0: tabWidth.
	uint32_t indentSize = 0;
	bool insertSpaces {};

	bool operator==(const FileOptions &other) const = default;
};


// The attribute holding the options: a flattened BMessage.
constexpr const char *kFileOptionsAttribute = "CodeEdit:options";

// The attribute's data; options with missing or invalid fields keep their
// defaults, data that is no message gives none.
std::string FlattenFileOptions(const FileOptions &options);
std::optional<FileOptions> UnflattenFileOptions(std::string_view data);


} // Workspace
