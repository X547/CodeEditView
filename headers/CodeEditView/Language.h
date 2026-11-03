#pragma once

#include <string>
#include <string_view>
#include <vector>


namespace CodeEdit {


// Names of the built-in languages, usable with TextModel::SetLanguage().
std::vector<std::string> AvailableLanguages();

// Language for a file path from its extension, or nullptr for plain text.
const char *LanguageForPath(std::string_view path);


} // CodeEdit
