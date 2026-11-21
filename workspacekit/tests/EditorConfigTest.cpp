#include <catch2/catch_test_macros.hpp>

#include "EditorConfig.h"
#include "TestSupport.h"


namespace Workspace {


TEST_CASE("EditorConfig globs")
{
	CHECK(MatchEditorConfigGlob("*", "a.c"));
	CHECK(MatchEditorConfigGlob("*", "src/a.c"));
	CHECK(MatchEditorConfigGlob("*.c", "src/deep/a.c"));
	CHECK_FALSE(MatchEditorConfigGlob("*.c", "a.cpp"));
	CHECK(MatchEditorConfigGlob("*.{c,h}", "a.h"));
	CHECK_FALSE(MatchEditorConfigGlob("*.{c,h}", "a.hh"));
	CHECK(MatchEditorConfigGlob("{Makefile,*.mk}", "sub/rules.mk"));
	CHECK(MatchEditorConfigGlob("Makefile", "sub/Makefile"));
	CHECK(MatchEditorConfigGlob("?.c", "a.c"));
	CHECK_FALSE(MatchEditorConfigGlob("?.c", "ab.c"));
	CHECK(MatchEditorConfigGlob("[ab].c", "b.c"));
	CHECK_FALSE(MatchEditorConfigGlob("[!ab].c", "b.c"));
	CHECK(MatchEditorConfigGlob("[a-c]x", "bx"));
	CHECK(MatchEditorConfigGlob("file{1..3}.txt", "file2.txt"));
	CHECK_FALSE(MatchEditorConfigGlob("file{1..3}.txt", "file4.txt"));
	CHECK(MatchEditorConfigGlob("{single}.c", "{single}.c"));
	CHECK(MatchEditorConfigGlob("\\*.c", "*.c"));
	CHECK_FALSE(MatchEditorConfigGlob("\\*.c", "a.c"));

	// With a slash: relative to the directory of the file.
	CHECK(MatchEditorConfigGlob("src/*.c", "src/a.c"));
	CHECK_FALSE(MatchEditorConfigGlob("src/*.c", "src/deep/a.c"));
	CHECK(MatchEditorConfigGlob("src/**.c", "src/deep/a.c"));
	CHECK(MatchEditorConfigGlob("/lib/*", "lib/x"));
	CHECK_FALSE(MatchEditorConfigGlob("/lib/*", "other/lib/x"));
}


TEST_CASE("EditorConfig sections and properties")
{
	const char *text =
		"# comment\n"
		"root = true\n"
		"\n"
		"[*]\n"
		"indent_style = tab\n"
		"Indent_Size = 4\n"
		"\n"
		"[*.py]\n"
		"indent_style = space\n"
		"\n"
		"[Makefile]\n"
		"indent_size = unset\n";
	EditorConfigProperties properties;
	CHECK(ApplyEditorConfig(text, "src/a.py", properties));
	CHECK(properties["indent_style"] == "space");
	CHECK(properties["indent_size"] == "4");

	properties.clear();
	CHECK(ApplyEditorConfig(text, "Makefile", properties));
	CHECK(properties.count("indent_size") == 0);

	properties.clear();
	CHECK_FALSE(ApplyEditorConfig("[*]\nroot = true\n", "a", properties));
}


TEST_CASE("EditorConfig indentation")
{
	EditorConfig config = ResolveEditorConfig({{"indent_style", "space"}, {"indent_size", "2"}});
	CHECK(config.insertSpaces == true);
	CHECK(config.indentSize == 2u);
	CHECK(config.tabWidth == 2u);

	config = ResolveEditorConfig({{"indent_style", "tab"}, {"indent_size", "tab"}, {"tab_width", "8"}});
	CHECK(config.insertSpaces == false);
	CHECK(config.indentSize == 0u);
	CHECK(config.tabWidth == 8u);

	config = ResolveEditorConfig({{"indent_style", "other"}, {"indent_size", "x"}, {"tab_width", "1000"}});
	CHECK(config.IsEmpty());
}


TEST_CASE("EditorConfig files are read up to the root")
{
	TempDirectory directory;
	directory.Write(".editorconfig", "[*]\nindent_style = space\ntab_width = 8\n");
	directory.Write("project/.editorconfig", "root = true\n[*]\nindent_style = tab\n[*.c]\nindent_size = 3\n");
	directory.Write("project/src/.editorconfig", "[*.c]\nindent_style = space\n");

	EditorConfig config = ReadEditorConfig(directory / "project/src/a.c");
	CHECK(config.insertSpaces == true);
	CHECK(config.indentSize == 3u);
	// The outer file is above the root.
	CHECK(config.tabWidth == 3u);

	config = ReadEditorConfig(directory / "project/b.txt");
	CHECK(config.insertSpaces == false);
	CHECK_FALSE(config.indentSize.has_value());

	config = ReadEditorConfig(directory / "other.txt");
	CHECK(config.insertSpaces == true);
	CHECK(config.tabWidth == 8u);
}


} // Workspace
