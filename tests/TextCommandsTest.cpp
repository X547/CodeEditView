#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/EditorSettings.h>
#include <CodeEditView/TextCommands.h>


namespace CodeEdit {


static std::string Run(const TextCommand &command, const std::string &text, uint32_t tabWidth = 4)
{
	EditorSettings settings;
	settings.tabWidth = tabWidth;
	TextModel model;
	ModelLock lock(model);
	model.SetText(text);
	TextCommands::Execute(model, 1, command, settings, {});
	return model.Text().ToString();
}


TEST_CASE("cleanup whitespace trims line ends and the end of the text")
{
	const TextCommand command = TextCommands::CleanupWhitespace;
	CHECK(Run(command, "a  \n\tb\t \nc") == "a\n\tb\nc\n");
	CHECK(Run(command, "a\n\n \n\t\n") == "a\n");
	CHECK(Run(command, "a \t") == "a\n");
	CHECK(Run(command, "a\n\n  b\n") == "a\n\n  b\n");
	CHECK(Run(command, " \n\t\n") == "");
	CHECK(Run(command, "") == "");
}


TEST_CASE("cleanup whitespace changes nothing in clean text")
{
	const EditorSettings settings;
	CHECK(TextCommands::CleanupWhitespace(Rope("a\n\tb\n"), settings).empty());
	CHECK(TextCommands::CleanupWhitespace(Rope(""), settings).empty());
}


TEST_CASE("tabs become spaces up to their tab stops")
{
	const TextCommand command = TextCommands::TabsToSpaces;
	CHECK(Run(command, "\tx\n") == "    x\n");
	CHECK(Run(command, "ab\tc\t\td") == "ab  c       d");
	CHECK(Run(command, "a\tb", 8) == "a       b");
	// A wide character takes two cells.
	CHECK(Run(command, "\xe3\x81\x82\tb") == "\xe3\x81\x82  b");
	CHECK(TextCommands::TabsToSpaces(Rope("no tabs\n"), EditorSettings()).empty());
}


TEST_CASE("leading spaces become tabs")
{
	const TextCommand command = TextCommands::LeadingSpacesToTabs;
	CHECK(Run(command, "    a\n        b\n      c  d\n") == "\ta\n\t\tb\n\t  c  d\n");
	CHECK(Run(command, "  \tx") == "\tx");
	CHECK(Run(command, "\t    x") == "\t\tx");
	CHECK(Run(command, "        x", 8) == "\tx");
	CHECK(Run(command, "x    y") == "x    y");
	// Already as narrow as possible.
	CHECK(TextCommands::LeadingSpacesToTabs(Rope("\ta\n  b\n\t  c\n"), EditorSettings()).empty());
}


TEST_CASE("offsets are mapped through the changes")
{
	const std::vector<TextChange> changes {
		{2, 1, "    "},
		{6, 3, ""},
		{10, 0, "\n"},
	};
	CHECK(TextCommands::MapOffset(changes, 0) == 0);
	CHECK(TextCommands::MapOffset(changes, 2) == 2);
	CHECK(TextCommands::MapOffset(changes, 3) == 6);
	CHECK(TextCommands::MapOffset(changes, 5) == 8);
	CHECK(TextCommands::MapOffset(changes, 7) == 9);
	CHECK(TextCommands::MapOffset(changes, 9) == 9);
	CHECK(TextCommands::MapOffset(changes, 10) == 10);
	CHECK(TextCommands::MapOffset(changes, 11) == 12);
}


TEST_CASE("a text command is one undo step restoring the selection")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("a \nb \n\n");
	const EditorSettings settings;
	const Selection before {3, 4};

	const std::optional<Selection> after = TextCommands::Execute(
		model,
		7,
		TextCommands::CleanupWhitespace,
		settings,
		before
	);
	REQUIRE(after.has_value());
	CHECK(model.Text().ToString() == "a\nb\n");
	CHECK(*after == Selection {2, 3});

	const std::optional<UndoResult> undone = model.Undo();
	REQUIRE(undone.has_value());
	CHECK(model.Text().ToString() == "a \nb \n\n");
	CHECK(undone->owner == 7);
	CHECK(undone->selection == before);
	CHECK_FALSE(model.CanUndo());
}


TEST_CASE("a text command without changes leaves no undo step")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("a\n");
	const std::optional<Selection> after = TextCommands::Execute(
		model,
		7,
		TextCommands::CleanupWhitespace,
		EditorSettings(),
		{}
	);
	CHECK_FALSE(after.has_value());
	CHECK_FALSE(model.CanUndo());
}


} // CodeEdit
