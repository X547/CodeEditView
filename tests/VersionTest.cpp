#include <catch2/catch_test_macros.hpp>

#include <tree_sitter/api.h>

#include <CodeEditView/Version.h>


TEST_CASE("library version")
{
	const CodeEdit::Version version = CodeEdit::LibraryVersion();
	CHECK(version.major == 0);
	CHECK(version.minor == 1);
}


TEST_CASE("tree-sitter runtime is usable")
{
	CHECK(CodeEdit::TreeSitterLanguageVersion() >= TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);

	TSParser *parser = ts_parser_new();
	REQUIRE(parser != nullptr);
	ts_parser_delete(parser);
}
