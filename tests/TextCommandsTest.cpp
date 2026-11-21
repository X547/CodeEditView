#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/EditorSettings.h>
#include <CodeEditView/TextCommands.h>


namespace CodeEdit {


using Command = std::optional<Selection> (*)(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
);


static std::string Run(Command command, const std::string &text, uint32_t tabWidth = 4)
{
	EditorSettings settings;
	settings.tabWidth = tabWidth;
	TextModel model;
	ModelLock lock(model);
	model.SetText(text);
	command(model, 1, {}, settings);
	return model.Text().ToString();
}


static bool Changes(Command command, const std::string &text)
{
	TextModel model;
	ModelLock lock(model);
	model.SetText(text);
	return command(model, 1, {}, EditorSettings()).has_value() || model.CanUndo();
}


TEST_CASE("cleanup whitespace trims line ends and the end of the text")
{
	const Command command = TextCommands::CleanupWhitespace;
	CHECK(Run(command, "a  \n\tb\t \nc") == "a\n\tb\nc\n");
	CHECK(Run(command, "a\n\n \n\t\n") == "a\n");
	CHECK(Run(command, "a \t") == "a\n");
	CHECK(Run(command, "a\n\n  b\n") == "a\n\n  b\n");
	CHECK(Run(command, " \n\t\n") == "");
	CHECK(Run(command, "") == "");
	CHECK_FALSE(Changes(command, "a\n\tb\n"));
	CHECK_FALSE(Changes(command, ""));
}


TEST_CASE("tabs become spaces up to their tab stops")
{
	const Command command = TextCommands::TabsToSpaces;
	CHECK(Run(command, "\tx\n") == "    x\n");
	CHECK(Run(command, "ab\tc\t\td") == "ab  c       d");
	CHECK(Run(command, "a\tb", 8) == "a       b");
	// A wide character takes two cells.
	CHECK(Run(command, "\xe3\x81\x82\tb") == "\xe3\x81\x82  b");
	CHECK_FALSE(Changes(command, "no tabs\n"));
}


TEST_CASE("leading spaces become tabs")
{
	const Command command = TextCommands::LeadingSpacesToTabs;
	CHECK(Run(command, "    a\n        b\n      c  d\n") == "\ta\n\t\tb\n\t  c  d\n");
	CHECK(Run(command, "  \tx") == "\tx");
	CHECK(Run(command, "\t    x") == "\t\tx");
	CHECK(Run(command, "        x", 8) == "\tx");
	CHECK(Run(command, "x    y") == "x    y");
	// Already as narrow as possible.
	CHECK_FALSE(Changes(command, "\ta\n  b\n\t  c\n"));
}


TEST_CASE("a text command is one named undo step restoring the selection")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("a \nb \n\n");
	const Selection before {3, 4};

	const std::optional<Selection> after = TextCommands::CleanupWhitespace(model, 7, before, EditorSettings());
	REQUIRE(after.has_value());
	CHECK(model.Text().ToString() == "a\nb\n");
	CHECK(*after == Selection {2, 3});
	CHECK(model.UndoName() == "Clean up whitespace");

	const std::optional<UndoResult> undone = model.Undo();
	REQUIRE(undone.has_value());
	CHECK(model.Text().ToString() == "a \nb \n\n");
	CHECK(undone->owner == 7);
	CHECK(undone->selection == before);
	CHECK_FALSE(model.CanUndo());

	const std::optional<UndoResult> redone = model.Redo();
	REQUIRE(redone.has_value());
	CHECK(redone->selection == Selection {2, 3});
}


TEST_CASE("the selection follows the edits of a text command")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("a\tb\tc");
	// The caret after the second tab stays after its spaces.
	std::optional<Selection> after = TextCommands::TabsToSpaces(model, 1, {4, 4}, EditorSettings());
	REQUIRE(after.has_value());
	CHECK(model.Text().ToString() == "a   b   c");
	CHECK(*after == Selection {8, 8});
	CHECK(model.UndoName() == "Convert tabs to spaces");

	model.SetText("    x");
	after = TextCommands::LeadingSpacesToTabs(model, 1, {5, 5}, EditorSettings());
	REQUIRE(after.has_value());
	CHECK(*after == Selection {2, 2});
	CHECK(model.UndoName() == "Convert leading spaces to tabs");
}


TEST_CASE("a text command without changes leaves no undo step")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("a\n");
	CHECK_FALSE(TextCommands::CleanupWhitespace(model, 7, {}, EditorSettings()).has_value());
	CHECK_FALSE(model.CanUndo());
}


} // CodeEdit
