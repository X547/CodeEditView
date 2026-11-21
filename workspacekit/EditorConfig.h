#pragma once

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>


namespace Workspace {


// The indentation an EditorConfig file (https://editorconfig.org) gives a
// file; none for what it does not set.
struct EditorConfig {
	std::optional<bool> insertSpaces;
	std::optional<uint32_t> tabWidth;
	// 0: the tab width (indent_size = tab).
	std::optional<uint32_t> indentSize;

	bool IsEmpty() const {return !insertSpaces.has_value() && !tabWidth.has_value() && !indentSize.has_value();}
	bool operator==(const EditorConfig &other) const = default;
};


using EditorConfigProperties = std::map<std::string, std::string>;


// Whether a section name matches a path relative to the directory of the
// .editorconfig file. A glob without "/" matches the file name in any
// directory below. Supports "*", "**", "?", "[seq]", "[!seq]", "{a,b}",
// "{num1..num2}" and "\" escapes.
bool MatchEditorConfigGlob(std::string_view glob, std::string_view relativePath);

// Applies the sections of one file's text that match relativePath to the
// properties (names and values in lower case; "unset" removes one). Returns
// whether the file says root = true.
bool ApplyEditorConfig(std::string_view text, std::string_view relativePath, EditorConfigProperties &properties);

// The indentation of the properties: indent_style, indent_size and tab_width
// (which defaults to a numeric indent_size).
EditorConfig ResolveEditorConfig(const EditorConfigProperties &properties);

// Reads the .editorconfig files of the absolute path's directory and of the
// directories above it, up to one with root = true; nearer files win.
EditorConfig ReadEditorConfig(const std::string &path);


} // Workspace
