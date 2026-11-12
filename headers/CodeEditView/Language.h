#pragma once

#include <string>
#include <string_view>
#include <vector>


namespace CodeEdit {


// Names of the built-in languages, usable with TextModel::SetLanguage().
std::vector<std::string> AvailableLanguages();

// Language for a file path from its extension, or nullptr for plain text.
const char *LanguageForPath(std::string_view path);

// Language for a MIME type, such as "text/x-makefile", or nullptr if the type
// names none.
const char *LanguageForMimeType(std::string_view type);

// Language for a file: from its MIME type (the BEOS:TYPE attribute) if that
// names a language, otherwise from its path; nullptr for no path. Reads the
// file's attributes, so call it where the file is read.
const char *LanguageForFile(const char *path);


} // CodeEdit
