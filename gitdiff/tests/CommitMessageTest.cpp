#include <string>

#include <catch2/catch_test_macros.hpp>

#include "CommitMessage.h"


namespace GitDiff {


using CodeEdit::Decoration;
using CodeEdit::DecorationKind;


TEST_CASE("Change-Id")
{
	const std::string id = "I0123456789abcdef0123456789abcdef01234567";
	CHECK(AddChangeId("Subject\n", id) == "Subject\n\nChange-Id: " + id + "\n");
	CHECK(AddChangeId("Subject\n\nBody text.\n", id) == "Subject\n\nBody text.\n\nChange-Id: " + id + "\n");
	// Into the trailer block, before Signed-off-by.
	CHECK(AddChangeId("Subject\n\nBody.\n\nBug: 12\nSigned-off-by: A <a@b>\n", id)
		== "Subject\n\nBody.\n\nBug: 12\nChange-Id: " + id + "\nSigned-off-by: A <a@b>\n");
	CHECK(AddChangeId("Subject\n\nFixes: 12\n  more\n", id) == "Subject\n\nFixes: 12\n  more\nChange-Id: " + id + "\n");
	// A body paragraph with a colon is not a trailer block.
	CHECK(AddChangeId("Subject\n\nNote: this and\nthat.\n", id)
		== "Subject\n\nNote: this and\nthat.\n\nChange-Id: " + id + "\n");
	// The subject alone is not a trailer block.
	CHECK(AddChangeId("Fix: x\n", id) == "Fix: x\n\nChange-Id: " + id + "\n");

	const std::string existing = "Subject\n\nChange-Id: Iaaaa\n";
	CHECK(AddChangeId(existing, id) == existing);
	CHECK(AddChangeId("fixup! Subject\n", id) == "fixup! Subject\n");
	CHECK(AddChangeId("squash! Subject\n", id) == "squash! Subject\n");
}


TEST_CASE("commit message decorations")
{
	CHECK(CommitMessageDecorations("Short subject\n\nBody.\n").empty());
	CHECK(CommitMessageDecorations("").empty());

	// The subject is marked from column 50, counting characters.
	const std::string subject = "é" + std::string(49, 's') + "TOO LONG";
	std::vector<Decoration> decorations = CommitMessageDecorations(subject + "\n");
	REQUIRE(decorations.size() == 1);
	CHECK(decorations[0].kind == DecorationKind::RangeBackground);
	CHECK(decorations[0].start == 51);
	CHECK(decorations[0].end == subject.size());

	// Text on the second line, and a body line past column 72.
	const std::string body = std::string(72, 'b') + "xx";
	decorations = CommitMessageDecorations("Subject\nnot blank\n" + body);
	REQUIRE(decorations.size() == 2);
	CHECK(decorations[0].kind == DecorationKind::LineBackground);
	CHECK(decorations[0].start == 8);
	CHECK(decorations[1].kind == DecorationKind::RangeBackground);
	CHECK(decorations[1].start == 18 + 72);
	CHECK(decorations[1].end == 18 + body.size());

	// A tab advances to the next multiple of 8.
	decorations = CommitMessageDecorations("S\n\n\t" + std::string(64, 'b') + "x");
	REQUIRE(decorations.size() == 1);
	CHECK(decorations[0].start == 3 + 1 + 64);
}


TEST_CASE("commit message settings")
{
	const CodeEdit::EditorSettings settings = CommitMessageSettings();
	CHECK(settings.marginColumn == kBodyLimit);
	CHECK(settings.hardWrapColumn == kBodyLimit);
	CHECK(settings.hardWrapFirstLine == 1);
	CHECK(settings.wordWrap);
}


} // GitDiff
