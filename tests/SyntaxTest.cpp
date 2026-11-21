#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <File.h>
#include <Node.h>
#include <NodeInfo.h>
#include <OS.h>

#include <CodeEditView/Language.h>
#include <CodeEditView/TextModel.h>

#include "syntax/Language.h"
#include "view/EditorCore.h"


namespace CodeEdit {


static constexpr const char *kSample =
	"#include <stdio.h>\n"
	"// note\n"
	"int main(int argc, char **argv)\n"
	"{\n"
	"\tconst char *s = \"a(b\";\n"
	"\treturn 'x' + 42;\n"
	"}\n";


static BReference<TextModel> MakeModel(std::string_view text, const char *language)
{
	BReference<TextModel> model(new TextModel(), true);
	ModelLock lock(*model);
	model->SetText(text);
	model->SetLanguage(language);
	return model;
}


static uint64_t SyntaxRevision(TextModel &model)
{
	ModelLock lock(model);
	return model.SyntaxRevision();
}


// Waits for the background parser to publish a tree newer than revision.
static bool WaitForSyntax(TextModel &model, uint64_t revision)
{
	for (int i = 0; i < 500; i++) {
		if (SyntaxRevision(model) > revision) {
			return true;
		}
		snooze(10000);
	}
	return false;
}


static BReference<TextModel> MakeParsedModel(std::string_view text, const char *language)
{
	BReference<TextModel> model(new TextModel(), true);
	uint64_t revision;
	{
		ModelLock lock(*model);
		model->SetText(text);
		model->SetLanguage(language);
		revision = model->SyntaxRevision();
	}
	REQUIRE(WaitForSyntax(*model, revision));
	return model;
}


static size_t LineStart(TextModel &model, size_t line)
{
	ModelLock lock(model);
	return model.Text().LineStart(line);
}


static HighlightStyle StyleAt(EditorCore &core, size_t row, size_t offset)
{
	const std::vector<RowData> rows = core.Rows(row, 1);
	REQUIRE(rows.size() == 1);
	if (rows.front().styles.empty()) {
		return HighlightStyle::Default;
	}
	REQUIRE(offset < rows.front().styles.size());
	return rows.front().styles[offset];
}


TEST_CASE("languages and capture styles")
{
	const std::vector<std::string> languages = AvailableLanguages();
	CHECK(std::find(languages.begin(), languages.end(), "c") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "cpp") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "shell") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "meson") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "jam") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "componentpascal") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "python") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "make") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "cmake") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "devicetree") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "html") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "css") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "javascript") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "typescript") != languages.end());
	CHECK(std::find(languages.begin(), languages.end(), "tsx") != languages.end());
	for (const std::string &name : languages) {
		const Language *language = Language::ForName(name);
		REQUIRE(language != nullptr);
		CHECK(language->Highlights() != nullptr);
		CHECK(language->Navigation() != nullptr);
	}
	CHECK(Language::ForName("cobol") == nullptr);

	CHECK(std::string_view(LanguageForPath("src/main.c")) == "c");
	CHECK(std::string_view(LanguageForPath("/boot/x/View.CPP")) == "cpp");
	CHECK(std::string_view(LanguageForPath("Header.h")) == "cpp");
	CHECK(std::string_view(LanguageForPath("tools/run.sh")) == "shell");
	CHECK(std::string_view(LanguageForPath("/boot/home/.bashrc")) == "shell");
	CHECK(std::string_view(LanguageForPath("meson.build")) == "meson");
	CHECK(std::string_view(LanguageForPath("src/meson.build")) == "meson");
	CHECK(std::string_view(LanguageForPath("meson_options.txt")) == "meson");
	CHECK(std::string_view(LanguageForPath("src/apps/Jamfile")) == "jam");
	CHECK(std::string_view(LanguageForPath("Jamrules")) == "jam");
	CHECK(std::string_view(LanguageForPath("rules.jam")) == "jam");
	CHECK(std::string_view(LanguageForPath("/haiku/build/jam/ArchitectureRules")) == "jam");
	CHECK(std::string_view(LanguageForPath("build/jam/packages/Haiku")) == "jam");
	CHECK(std::string_view(LanguageForPath("build/jam/tool.sh")) == "shell");
	CHECK(LanguageForPath("mybuild/jam/Rules") == nullptr);
	CHECK(std::string_view(LanguageForPath("Obx/Mod/Hello0.cp")) == "componentpascal");
	CHECK(LanguageForPath("Obx/Mod/Hello0.Mod") == nullptr);
	CHECK(std::string_view(LanguageForPath("tools/embed_text.py")) == "python");
	CHECK(std::string_view(LanguageForPath("src/Makefile")) == "make");
	CHECK(std::string_view(LanguageForPath("GNUmakefile")) == "make");
	CHECK(std::string_view(LanguageForPath("rules.mk")) == "make");
	CHECK(std::string_view(LanguageForPath("/boot/system/develop/etc/makefile-engine")) == "make");
	CHECK(std::string_view(LanguageForPath("src/CMakeLists.txt")) == "cmake");
	CHECK(std::string_view(LanguageForPath("FindFoo.cmake")) == "cmake");
	CHECK(LanguageForPath("cmakelists.txt") == nullptr);
	CHECK(std::string_view(LanguageForPath("arch/arm/boot/dts/board.dts")) == "devicetree");
	CHECK(std::string_view(LanguageForPath("soc.DTSI")) == "devicetree");
	CHECK(std::string_view(LanguageForPath("fix.dtso")) == "devicetree");
	CHECK(std::string_view(LanguageForPath("boards/nrf52840dk.overlay")) == "devicetree");
	CHECK(std::string_view(LanguageForPath("docs/index.html")) == "html");
	CHECK(std::string_view(LanguageForPath("INDEX.HTM")) == "html");
	CHECK(std::string_view(LanguageForPath("style.css")) == "css");
	CHECK(std::string_view(LanguageForPath("app.js")) == "javascript");
	CHECK(std::string_view(LanguageForPath("module.mjs")) == "javascript");
	CHECK(std::string_view(LanguageForPath("App.jsx")) == "javascript");
	CHECK(std::string_view(LanguageForPath("src/main.ts")) == "typescript");
	CHECK(std::string_view(LanguageForPath("config.mts")) == "typescript");
	CHECK(std::string_view(LanguageForPath("src/App.tsx")) == "tsx");
	CHECK(LanguageForPath("README") == nullptr);
	CHECK(LanguageForPath("x.build") == nullptr);
	CHECK(LanguageForPath("notes.txt") == nullptr);
	CHECK(LanguageForPath(".gitignore") == nullptr);
	CHECK(LanguageForPath("dir.c/file") == nullptr);

	CHECK(StyleForCapture("type.builtin") == HighlightStyle::BuiltinType);
	CHECK(StyleForCapture("type.qualifier") == HighlightStyle::Type);
	CHECK(StyleForCapture("keyword.control.flow") == HighlightStyle::Keyword);
	CHECK(StyleForCapture("variable") == HighlightStyle::Default);
}


TEST_CASE("languages from MIME types")
{
	CHECK(std::string_view(LanguageForMimeType("text/x-jamfile")) == "jam");
	CHECK(std::string_view(LanguageForMimeType("Text/X-Makefile")) == "make");
	CHECK(std::string_view(LanguageForMimeType("text/html")) == "html");
	CHECK(std::string_view(LanguageForMimeType("text/css")) == "css");
	CHECK(std::string_view(LanguageForMimeType("text/javascript")) == "javascript");
	CHECK(std::string_view(LanguageForMimeType("application/javascript")) == "javascript");
	CHECK(LanguageForMimeType("text/plain") == nullptr);
	CHECK(LanguageForMimeType("") == nullptr);

	char directory[] = "/tmp/CodeEditTest-XXXXXX";
	REQUIRE(mkdtemp(directory) != nullptr);
	const std::string path = std::string(directory) + "/rules.py";
	{
		BFile file(path.c_str(), B_WRITE_ONLY | B_CREATE_FILE);
		REQUIRE(file.InitCheck() == B_OK);
	}
	// Without a type, from the path.
	CHECK(std::string_view(LanguageForFile(path.c_str())) == "python");
	// The type takes priority.
	{
		BNode node(path.c_str());
		BNodeInfo info(&node);
		REQUIRE(info.SetType("text/x-makefile") == B_OK);
	}
	CHECK(std::string_view(LanguageForFile(path.c_str())) == "make");
	// A type that names no language leaves the path.
	{
		BNode node(path.c_str());
		BNodeInfo info(&node);
		REQUIRE(info.SetType("text/plain") == B_OK);
	}
	CHECK(std::string_view(LanguageForFile(path.c_str())) == "python");
	CHECK(std::string_view(LanguageForFile((std::string(directory) + "/missing.mk").c_str())) == "make");
	CHECK(LanguageForFile(nullptr) == nullptr);
	unlink(path.c_str());
	rmdir(directory);
}


TEST_CASE("highlighting follows Pe styles")
{
	BReference<TextModel> model = MakeParsedModel(kSample, "cpp");
	EditorCore core(model);
	{
		ModelLock lock(*model);
		CHECK(std::string_view(model->LanguageName()) == "cpp");
	}

	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Preprocessor);
	CHECK(StyleAt(core, 0, 9) == HighlightStyle::String);
	CHECK(StyleAt(core, 1, 3) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 2, 4) == HighlightStyle::Default);
	CHECK(StyleAt(core, 2, 8) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 24) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 4, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 7) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 4, 15) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 4, 19) == HighlightStyle::String);
	CHECK(StyleAt(core, 4, 22) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 5, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 5, 9) == HighlightStyle::Character);
	CHECK(StyleAt(core, 5, 12) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 5, 14) == HighlightStyle::Number);
}


TEST_CASE("shell highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"f() {\n"
		"\tlocal a=\"x $HOME\" # c\n"
		"\tif [ -n \"$a\" ]; then return 2; fi\n"
		"\techo \"${a}\"\n"
		"}\n",
		"shell"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Function);
	CHECK(StyleAt(core, 0, 1) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 1, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 1, 7) == HighlightStyle::Default);
	CHECK(StyleAt(core, 1, 8) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 1, 10) == HighlightStyle::String);
	CHECK(StyleAt(core, 1, 12) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 1, 14) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 1, 17) == HighlightStyle::String);
	CHECK(StyleAt(core, 1, 19) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 4) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 6) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 2, 9) == HighlightStyle::String);
	CHECK(StyleAt(core, 2, 10) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 2, 17) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 22) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 29) == HighlightStyle::Number);
	CHECK(StyleAt(core, 2, 32) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 1) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 6) == HighlightStyle::String);
	CHECK(StyleAt(core, 3, 7) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 10) == HighlightStyle::Constant);
}


TEST_CASE("meson highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"project('x', 'cpp')\n"
		"# note\n"
		"cc = meson.get_compiler('cpp')\n"
		"if cc.found() and not false\n"
		"\tmessage(f'@VAR@\\n', 42)\n"
		"endif\n",
		"meson"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Function);
	CHECK(StyleAt(core, 0, 7) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 0, 8) == HighlightStyle::String);
	CHECK(StyleAt(core, 0, 11) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Default);
	CHECK(StyleAt(core, 2, 3) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 2, 5) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 2, 10) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 11) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 3) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 6) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 14) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 18) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 22) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 1) == HighlightStyle::Function);
	CHECK(StyleAt(core, 4, 10) == HighlightStyle::String);
	CHECK(StyleAt(core, 4, 11) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 16) == HighlightStyle::Character);
	CHECK(StyleAt(core, 4, 21) == HighlightStyle::Number);
	CHECK(StyleAt(core, 5, 0) == HighlightStyle::Keyword);
}


TEST_CASE("jam highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"# note\n"
		"rule Foo a : b\n"
		"{\n"
		"\tlocal x = $(a:S=.o)/y \"s $(b)\" ;\n"
		"\tif ! $(x) in c { return [ Bar $(x) ] ; }\n"
		"\tEcho if on ;\n"
		"}\n"
		"actions quietly Foo\n"
		"{\n"
		"\techo $(1) # not a comment\n"
		"}\n",
		"jam"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 1, 5) == HighlightStyle::Function);
	CHECK(StyleAt(core, 1, 9) == HighlightStyle::Default);
	CHECK(StyleAt(core, 1, 11) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 7) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 9) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 3, 11) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 16) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 19) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 20) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 23) == HighlightStyle::String);
	CHECK(StyleAt(core, 3, 24) == HighlightStyle::String);
	CHECK(StyleAt(core, 3, 26) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 30) == HighlightStyle::String);
	CHECK(StyleAt(core, 3, 32) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 4) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 4, 6) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 11) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 14) == HighlightStyle::Default);
	CHECK(StyleAt(core, 4, 16) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 18) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 25) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 27) == HighlightStyle::Function);
	CHECK(StyleAt(core, 4, 31) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 38) == HighlightStyle::Punctuation);
	// Keywords in lists are words, as in Jam.
	CHECK(StyleAt(core, 5, 1) == HighlightStyle::Function);
	CHECK(StyleAt(core, 5, 6) == HighlightStyle::Default);
	CHECK(StyleAt(core, 5, 9) == HighlightStyle::Default);
	CHECK(StyleAt(core, 7, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 7, 8) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 7, 16) == HighlightStyle::Function);
	// Action bodies are shell commands.
	CHECK(StyleAt(core, 9, 1) == HighlightStyle::Default);
	CHECK(StyleAt(core, 9, 6) == HighlightStyle::Default);
	CHECK(StyleAt(core, 9, 11) == HighlightStyle::Default);
}


TEST_CASE("component pascal highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"MODULE Test;\n"
		"(* c (* n *) *)\n"
		"\tIMPORT Views;\n"
		"\tCONST s = {0..9}; r = 1.5E3;\n"
		"\tTYPE V* = POINTER TO RECORD [untagged] (Views.View) n: INTEGER END;\n"
		"\tPROCEDURE (v: V) Do*;\n"
		"\tBEGIN v.n := 0FFH + ORD(0DX); Log(\"s\"); IF TRUE THEN END\n"
		"\tEND Do;\n"
		"END Test.\n"
		"Commands\n",
		"componentpascal"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 7) == HighlightStyle::Default);
	// Nested comments.
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 1, 13) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 1) == HighlightStyle::Keyword);
	// A range is not a real number.
	CHECK(StyleAt(core, 3, 12) == HighlightStyle::Number);
	CHECK(StyleAt(core, 3, 13) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 15) == HighlightStyle::Number);
	CHECK(StyleAt(core, 3, 23) == HighlightStyle::Number);
	CHECK(StyleAt(core, 3, 27) == HighlightStyle::Number);
	CHECK(StyleAt(core, 4, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 6) == HighlightStyle::Default);
	CHECK(StyleAt(core, 4, 7) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 4, 11) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 22) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 29) == HighlightStyle::Preprocessor);
	CHECK(StyleAt(core, 4, 30) == HighlightStyle::Preprocessor);
	CHECK(StyleAt(core, 4, 38) == HighlightStyle::Preprocessor);
	CHECK(StyleAt(core, 4, 41) == HighlightStyle::Type);
	CHECK(StyleAt(core, 4, 47) == HighlightStyle::Type);
	CHECK(StyleAt(core, 4, 53) == HighlightStyle::Default);
	CHECK(StyleAt(core, 4, 56) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 4, 64) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 5, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 5, 11) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 5, 12) == HighlightStyle::Default);
	CHECK(StyleAt(core, 5, 15) == HighlightStyle::Type);
	CHECK(StyleAt(core, 5, 18) == HighlightStyle::Function);
	CHECK(StyleAt(core, 5, 20) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 6, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 6, 7) == HighlightStyle::Default);
	CHECK(StyleAt(core, 6, 8) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 6, 11) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 6, 14) == HighlightStyle::Number);
	CHECK(StyleAt(core, 6, 19) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 6, 21) == HighlightStyle::Function);
	CHECK(StyleAt(core, 6, 25) == HighlightStyle::Character);
	CHECK(StyleAt(core, 6, 31) == HighlightStyle::Function);
	CHECK(StyleAt(core, 6, 35) == HighlightStyle::String);
	CHECK(StyleAt(core, 6, 41) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 6, 44) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 6, 54) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 7, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 7, 5) == HighlightStyle::Function);
	CHECK(StyleAt(core, 8, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 8, 4) == HighlightStyle::Default);
	CHECK(StyleAt(core, 8, 8) == HighlightStyle::Punctuation);
	// Text after the module.
	CHECK(StyleAt(core, 9, 0) == HighlightStyle::Comment);
}


TEST_CASE("python highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"import os\n"
		"# note\n"
		"class Point(Base):\n"
		"    def norm(self, x: int = 1) -> float:\n"
		"        return f\"{x} \\n\" if x is not None else 0x1F\n"
		"print(len(os.sep))\n",
		"python"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 7) == HighlightStyle::Default);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 6) == HighlightStyle::Type);
	CHECK(StyleAt(core, 2, 11) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 12) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 4) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 8) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 13) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 20) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 22) == HighlightStyle::Type);
	CHECK(StyleAt(core, 3, 26) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 3, 28) == HighlightStyle::Number);
	CHECK(StyleAt(core, 3, 31) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 3, 34) == HighlightStyle::Type);
	CHECK(StyleAt(core, 4, 8) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 15) == HighlightStyle::String);
	// Interpolations in f-strings are code.
	CHECK(StyleAt(core, 4, 17) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 18) == HighlightStyle::Default);
	CHECK(StyleAt(core, 4, 20) == HighlightStyle::String);
	CHECK(StyleAt(core, 4, 21) == HighlightStyle::Character);
	CHECK(StyleAt(core, 4, 23) == HighlightStyle::String);
	CHECK(StyleAt(core, 4, 25) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 30) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 33) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 37) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 42) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 47) == HighlightStyle::Number);
	CHECK(StyleAt(core, 5, 0) == HighlightStyle::Function);
	CHECK(StyleAt(core, 5, 6) == HighlightStyle::Function);
	CHECK(StyleAt(core, 5, 10) == HighlightStyle::Default);
	CHECK(StyleAt(core, 5, 13) == HighlightStyle::Default);
}


TEST_CASE("make highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"CC := gcc\n"
		"# note\n"
		"all: main.o $(OBJS)\n"
		"\t@$(CC) -o $@ $(dir $(file)) $(file)\n"
		"ifeq ($(OS),Haiku)\n"
		"LIBS += -lbe\n"
		"endif\n"
		".PHONY: all\n",
		"make"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Default);
	CHECK(StyleAt(core, 0, 3) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 0, 6) == HighlightStyle::Default);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Function);
	CHECK(StyleAt(core, 2, 3) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 5) == HighlightStyle::Default);
	CHECK(StyleAt(core, 2, 12) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 2, 14) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 2, 18) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 1) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 3, 2) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 4) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 8) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 11) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 14) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 16) == HighlightStyle::Function);
	// $(file) is a variable, not a call of the file function.
	CHECK(StyleAt(core, 3, 20) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 22) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 27) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 29) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 31) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 35) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 5) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 6) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 11) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 12) == HighlightStyle::Default);
	CHECK(StyleAt(core, 5, 0) == HighlightStyle::Default);
	CHECK(StyleAt(core, 5, 5) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 6, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 7, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 7, 8) == HighlightStyle::Default);
}


TEST_CASE("cmake highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"cmake_minimum_required(VERSION 3.10)\n"
		"# note\n"
		"function(add_thing name)\n"
		"  if(NOT TARGET ${name} AND ON)\n"
		"    message(STATUS \"x ${name}\\n\")\n"
		"    return()\n"
		"  endif()\n"
		"endfunction()\n"
		"#[[ block ]]\n",
		"cmake"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Function);
	CHECK(StyleAt(core, 0, 22) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 0, 23) == HighlightStyle::Default);
	CHECK(StyleAt(core, 0, 31) == HighlightStyle::Default);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 8) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 9) == HighlightStyle::Function);
	CHECK(StyleAt(core, 2, 19) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 2) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 5) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 9) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 16) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 18) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 22) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 24) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 28) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 4) == HighlightStyle::Function);
	CHECK(StyleAt(core, 4, 12) == HighlightStyle::Default);
	CHECK(StyleAt(core, 4, 19) == HighlightStyle::String);
	CHECK(StyleAt(core, 4, 22) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 4, 29) == HighlightStyle::Character);
	CHECK(StyleAt(core, 4, 31) == HighlightStyle::String);
	CHECK(StyleAt(core, 5, 4) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 6, 2) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 7, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 8, 0) == HighlightStyle::Comment);
}


TEST_CASE("devicetree highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"/dts-v1/;\n"
		"#include <dt-bindings/gpio/gpio.h>\n"
		"/ {\n"
		"  uart0: serial@1000 {\n"
		"    compatible = \"ns16550\";\n"
		"    reg = <0x1000 (1 + 2)>;\n"
		"    gpios = <&gpio0 GPIO(3) 0>;\n"
		"  };\n"
		"};\n"
		"// note\n"
		"&uart0 { status = \"okay\"; };\n",
		"devicetree"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 8) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Preprocessor);
	CHECK(StyleAt(core, 1, 9) == HighlightStyle::String);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Type);
	CHECK(StyleAt(core, 2, 2) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 2) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 3, 7) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 9) == HighlightStyle::Type);
	CHECK(StyleAt(core, 3, 15) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 16) == HighlightStyle::Number);
	CHECK(StyleAt(core, 4, 4) == HighlightStyle::Default);
	CHECK(StyleAt(core, 4, 15) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 17) == HighlightStyle::String);
	CHECK(StyleAt(core, 5, 10) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 5, 11) == HighlightStyle::Number);
	CHECK(StyleAt(core, 5, 18) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 5, 21) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 6, 13) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 6, 14) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 6, 20) == HighlightStyle::Function);
	CHECK(StyleAt(core, 6, 25) == HighlightStyle::Number);
	CHECK(StyleAt(core, 7, 2) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 9, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 10, 0) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 10, 1) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 10, 9) == HighlightStyle::Default);
}


TEST_CASE("html highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"<!DOCTYPE html>\n"
		"<!-- note -->\n"
		"<p class=\"a\">x &amp; y</p>\n"
		"<br/>\n"
		"<script>if (a) {}</script>\n",
		"html"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 2) == HighlightStyle::Preprocessor);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 3) == HighlightStyle::Type);
	CHECK(StyleAt(core, 2, 8) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 2, 9) == HighlightStyle::String);
	CHECK(StyleAt(core, 2, 12) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 13) == HighlightStyle::Default);
	CHECK(StyleAt(core, 2, 15) == HighlightStyle::Character);
	CHECK(StyleAt(core, 2, 22) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 2, 24) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 3) == HighlightStyle::Punctuation);
	// The script is not parsed.
	CHECK(StyleAt(core, 4, 1) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 8) == HighlightStyle::Default);
}


TEST_CASE("css highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"@media screen {\n"
		"  a.b:hover > #c::before { color: #fff !important; }\n"
		"}\n"
		"/* note */\n"
		".x { margin: 1.5em calc(2px + 3px); content: \"\\201C\"; }\n",
		"css"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Preprocessor);
	CHECK(StyleAt(core, 0, 14) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 1, 2) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 1, 3) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 1, 4) == HighlightStyle::Type);
	CHECK(StyleAt(core, 1, 5) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 1, 6) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 1, 12) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 1, 15) == HighlightStyle::Type);
	CHECK(StyleAt(core, 1, 18) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 1, 27) == HighlightStyle::Function);
	CHECK(StyleAt(core, 1, 32) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 1, 35) == HighlightStyle::Number);
	CHECK(StyleAt(core, 1, 39) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 1, 51) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 0) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 4, 1) == HighlightStyle::Type);
	CHECK(StyleAt(core, 4, 5) == HighlightStyle::Function);
	CHECK(StyleAt(core, 4, 13) == HighlightStyle::Number);
	CHECK(StyleAt(core, 4, 16) == HighlightStyle::Number);
	CHECK(StyleAt(core, 4, 19) == HighlightStyle::Function);
	CHECK(StyleAt(core, 4, 23) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 4, 28) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 4, 36) == HighlightStyle::Function);
	CHECK(StyleAt(core, 4, 45) == HighlightStyle::String);
	CHECK(StyleAt(core, 4, 46) == HighlightStyle::Character);
}


TEST_CASE("javascript highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"import { a } from \"./m.js\";\n"
		"const MAX_SIZE = 10, re = /x+/g;\n"
		"class Foo extends Bar {\n"
		"  get size() { return this.n; }\n"
		"}\n"
		"function f(x) { return new Foo(`a${x}b`); }\n"
		"const g = async (y) => console.log(y ?? null);\n"
		"// note\n"
		"let el = <div className=\"c\">&amp;<Foo /></div>;\n",
		"javascript"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 7) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 0, 9) == HighlightStyle::Default);
	CHECK(StyleAt(core, 0, 13) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 18) == HighlightStyle::String);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Keyword);
	// All capitals: a constant.
	CHECK(StyleAt(core, 1, 6) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 1, 15) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 1, 17) == HighlightStyle::Number);
	CHECK(StyleAt(core, 1, 21) == HighlightStyle::Default);
	CHECK(StyleAt(core, 1, 26) == HighlightStyle::String);
	CHECK(StyleAt(core, 1, 28) == HighlightStyle::String);
	CHECK(StyleAt(core, 1, 29) == HighlightStyle::String);
	CHECK(StyleAt(core, 1, 30) == HighlightStyle::String);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 6) == HighlightStyle::Type);
	CHECK(StyleAt(core, 2, 10) == HighlightStyle::Keyword);
	// Capitalized: a class.
	CHECK(StyleAt(core, 2, 18) == HighlightStyle::Type);
	CHECK(StyleAt(core, 3, 2) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 6) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 22) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 3, 26) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 27) == HighlightStyle::Default);
	CHECK(StyleAt(core, 5, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 5, 9) == HighlightStyle::Function);
	CHECK(StyleAt(core, 5, 23) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 5, 27) == HighlightStyle::Type);
	// A template string apart from its substitution.
	CHECK(StyleAt(core, 5, 31) == HighlightStyle::String);
	CHECK(StyleAt(core, 5, 32) == HighlightStyle::String);
	CHECK(StyleAt(core, 5, 33) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 5, 35) == HighlightStyle::Default);
	CHECK(StyleAt(core, 5, 36) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 5, 37) == HighlightStyle::String);
	CHECK(StyleAt(core, 5, 38) == HighlightStyle::String);
	CHECK(StyleAt(core, 6, 6) == HighlightStyle::Function);
	CHECK(StyleAt(core, 6, 10) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 6, 20) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 6, 23) == HighlightStyle::Default);
	CHECK(StyleAt(core, 6, 31) == HighlightStyle::Function);
	CHECK(StyleAt(core, 6, 37) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 6, 40) == HighlightStyle::Constant);
	CHECK(StyleAt(core, 7, 0) == HighlightStyle::Comment);
	// JSX: elements like HTML, components as types.
	CHECK(StyleAt(core, 8, 9) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 8, 10) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 8, 14) == HighlightStyle::Type);
	CHECK(StyleAt(core, 8, 24) == HighlightStyle::String);
	CHECK(StyleAt(core, 8, 27) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 8, 28) == HighlightStyle::Character);
	CHECK(StyleAt(core, 8, 33) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 8, 34) == HighlightStyle::Type);
	CHECK(StyleAt(core, 8, 38) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 8, 40) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 8, 42) == HighlightStyle::Keyword);
}


TEST_CASE("typescript highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"interface Point { x: number; readonly y?: string }\n"
		"type Id = string | number;\n"
		"enum Color { Red, Green }\n"
		"function id<T>(value: T): T { return value; }\n"
		"abstract class Shape implements Base { abstract area(): number; }\n"
		"let n = f<Id>(1) as any;\n",
		"typescript"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 10) == HighlightStyle::Type);
	CHECK(StyleAt(core, 0, 21) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 0, 29) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 42) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 1, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 1, 5) == HighlightStyle::Type);
	CHECK(StyleAt(core, 1, 10) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 1, 17) == HighlightStyle::Operator);
	CHECK(StyleAt(core, 2, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 2, 5) == HighlightStyle::Type);
	CHECK(StyleAt(core, 2, 13) == HighlightStyle::Default);
	CHECK(StyleAt(core, 3, 9) == HighlightStyle::Function);
	CHECK(StyleAt(core, 3, 11) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 3, 12) == HighlightStyle::Type);
	CHECK(StyleAt(core, 3, 15) == HighlightStyle::Default);
	CHECK(StyleAt(core, 4, 0) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 15) == HighlightStyle::Type);
	CHECK(StyleAt(core, 4, 21) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 4, 32) == HighlightStyle::Type);
	CHECK(StyleAt(core, 4, 48) == HighlightStyle::Function);
	CHECK(StyleAt(core, 5, 8) == HighlightStyle::Function);
	CHECK(StyleAt(core, 5, 9) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 5, 10) == HighlightStyle::Type);
	CHECK(StyleAt(core, 5, 17) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 5, 20) == HighlightStyle::BuiltinType);
}


TEST_CASE("tsx highlighting")
{
	BReference<TextModel> model = MakeParsedModel(
		"const App = (p: Props): JSX.Element => <div>{p.x as string}</div>;\n",
		"tsx"
	);
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 6) == HighlightStyle::Function);
	CHECK(StyleAt(core, 0, 16) == HighlightStyle::Type);
	CHECK(StyleAt(core, 0, 24) == HighlightStyle::Type);
	CHECK(StyleAt(core, 0, 39) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 0, 40) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 49) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 52) == HighlightStyle::BuiltinType);
	CHECK(StyleAt(core, 0, 59) == HighlightStyle::Punctuation);
	CHECK(StyleAt(core, 0, 61) == HighlightStyle::Keyword);
}


TEST_CASE("highlight predicates")
{
	BReference<TextModel> model = MakeParsedModel("int x = static_cast<int>(y) + other<int>(z);\n", "cpp");
	EditorCore core(model);
	CHECK(StyleAt(core, 0, 8) == HighlightStyle::Keyword);
	CHECK(StyleAt(core, 0, 30) == HighlightStyle::Default);

	BReference<TextModel> nested = MakeParsedModel(
		"static const char *F(void *payload)\n"
		"{\n"
		"\tconst Rope &rope = *static_cast<const Rope*>(payload);\n"
		"}\n",
		"cpp"
	);
	EditorCore nestedCore(nested);
	CHECK(StyleAt(nestedCore, 2, 21) == HighlightStyle::Keyword);
	// The view highlights all visible rows in one query.
	const std::vector<RowData> rows = nestedCore.Rows(0, 10);
	REQUIRE(rows.size() == 5);
	CHECK(rows[2].styles[21] == HighlightStyle::Keyword);
}


TEST_CASE("plain text after removing the language")
{
	BReference<TextModel> model = MakeParsedModel(kSample, "cpp");
	EditorCore core(model);
	CHECK_FALSE(core.Rows(0, 1).front().styles.empty());

	{
		ModelLock lock(*model);
		model->SetLanguage(nullptr);
	}
	CHECK(core.Rows(0, 1).front().styles.empty());
}


TEST_CASE("highlighting updates incrementally")
{
	BReference<TextModel> model = MakeParsedModel(kSample, "cpp");
	EditorCore core(model);
	CHECK(StyleAt(core, 5, 1) == HighlightStyle::Keyword);

	const uint64_t revision = SyntaxRevision(*model);
	core.SetSelection({LineStart(*model, 5) + 1, LineStart(*model, 5) + 1});
	core.InsertText("// ");
	REQUIRE(WaitForSyntax(*model, revision));
	CHECK(StyleAt(core, 5, 1) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 5, 9) == HighlightStyle::Comment);
	CHECK(StyleAt(core, 4, 1) == HighlightStyle::Keyword);

	const uint64_t undoRevision = SyntaxRevision(*model);
	CHECK(core.Undo());
	REQUIRE(WaitForSyntax(*model, undoRevision));
	CHECK(StyleAt(core, 5, 1) == HighlightStyle::Keyword);
}


TEST_CASE("brackets match by syntax")
{
	BReference<TextModel> model = MakeParsedModel(kSample, "cpp");
	EditorCore core(model);
	const size_t line2 = LineStart(*model, 2);
	const size_t line4 = LineStart(*model, 4);

	CHECK(core.BalancedRangeAt(line2 + 8) == TextRange {line2 + 9, line2 + 30});
	CHECK(core.BalancedRangeAt(line2 + 30) == TextRange {line2 + 9, line2 + 30});
	CHECK_FALSE(core.BalancedRangeAt(line4 + 19).has_value());
	core.SetSelection({line4 + 20, line4 + 20});
	core.UpdateBracketMatch();
	CHECK_FALSE(core.BracketMatch().has_value());

	// A top-level block is a syntax error; its braces still match.
	BReference<TextModel> invalid = MakeParsedModel("{\n\tf(a[1]);\n}\n", "cpp");
	EditorCore invalidCore(invalid);
	invalidCore.SetSelection({0, 0});
	invalidCore.UpdateBracketMatch();
	CHECK(invalidCore.BracketMatch() == BracketPair {0, 12});

	const size_t braceOpen = LineStart(*model, 3);
	const size_t braceClose = LineStart(*model, 6);
	CHECK(core.BalancedRangeAt(braceOpen) == TextRange {braceOpen + 1, braceClose});
}


static TextRange Range(const Selection &selection)
{
	return {std::min(selection.anchor, selection.caret), std::max(selection.anchor, selection.caret)};
}


TEST_CASE("expand and shrink selection by syntax node")
{
	BReference<TextModel> model = MakeParsedModel(kSample, "cpp");
	EditorCore core(model);
	const size_t line = LineStart(*model, 2);

	core.SetSelection({line + 14, line + 14});
	core.ExpandSelection();
	CHECK(Range(core.GetSelection()) == TextRange {line + 13, line + 17});
	core.ExpandSelection();
	CHECK(Range(core.GetSelection()) == TextRange {line + 9, line + 17});
	core.ExpandSelection();
	CHECK(Range(core.GetSelection()) == TextRange {line + 9, line + 30});
	core.ExpandSelection();
	CHECK(Range(core.GetSelection()) == TextRange {line + 8, line + 31});

	core.ShrinkSelection();
	CHECK(Range(core.GetSelection()) == TextRange {line + 9, line + 30});
	core.ShrinkSelection();
	core.ShrinkSelection();
	CHECK(Range(core.GetSelection()) == TextRange {line + 13, line + 17});
	core.ShrinkSelection();
	CHECK(core.GetSelection() == Selection {line + 14, line + 14});
	core.ShrinkSelection();
	CHECK(core.GetSelection() == Selection {line + 14, line + 14});

	core.ExpandSelection();
	core.SetSelection({0, 0});
	core.ShrinkSelection();
	CHECK(core.GetSelection() == Selection {0, 0});
}


TEST_CASE("move between definitions")
{
	BReference<TextModel> model = MakeParsedModel(
		"int a() { return 1; }\n"
		"struct S { int x; };\n"
		"int b() { return 2; }\n",
		"cpp"
	);
	EditorCore core(model);
	const size_t line1 = LineStart(*model, 1);
	const size_t line2 = LineStart(*model, 2);

	core.SetSelection({0, 0});
	core.MoveToDefinition(true);
	CHECK(core.GetSelection().caret == line1);
	core.MoveToDefinition(true);
	CHECK(core.GetSelection().caret == line2);
	core.MoveToDefinition(true);
	CHECK(core.GetSelection().caret == line2);
	core.MoveToDefinition(false);
	CHECK(core.GetSelection().caret == line1);
	core.MoveToDefinition(false);
	CHECK(core.GetSelection().caret == 0);
}


TEST_CASE("switching languages and destroying a model while parsing")
{
	std::string large;
	for (int i = 0; i < 20000; i++) {
		large += "int f" + std::to_string(i) + "(int x) { return x * 2; }\n";
	}

	for (int i = 0; i < 3; i++) {
		BReference<TextModel> model = MakeModel(large, "cpp");
		ModelLock lock(*model);
		model->SetLanguage("c");
		model->Insert(0, "// x\n");
		model->SetLanguage("cpp");
	}

	BReference<TextModel> model = MakeParsedModel(large, "c");
	{
		ModelLock lock(*model);
		model->SetLanguage(nullptr);
		CHECK(model->LanguageName() == nullptr);
	}
	EditorCore core(model);
	CHECK(core.Rows(0, 1).front().styles.empty());
}


} // CodeEdit
