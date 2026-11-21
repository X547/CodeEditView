#pragma once

#include <stddef.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <SupportDefs.h>

#include <CodeEditView/TextModel.h>


class BMenu;


namespace CodeEdit {


struct EditorSettings;


// Text commands: transformations of the whole text of the target editor (see
// EditorCommands). Each command is one undo step.
//
// Removes the spaces and tabs at the end of lines and the blank lines at the
// end of the text; a text that is not blank ends with one line break, a blank
// one becomes empty.
constexpr uint32 kMsgCleanupWhitespace = 'CEws';
// Replaces each tab with the spaces up to its tab stop.
constexpr uint32 kMsgTabsToSpaces = 'CEts';
// Writes the leading whitespace of each line as tabs, keeping its width;
// spaces remain only for a rest narrower than a tab.
constexpr uint32 kMsgLeadingSpacesToTabs = 'CEst';


// Replaces [offset, offset + length) of the original text with text.
struct TextChange {
	size_t offset {};
	size_t length {};
	std::string text;

	bool operator==(const TextChange &other) const = default;
};


// The changes a command makes to a text, ordered by offset and not
// overlapping; empty if the command changes nothing.
using TextCommand = std::function<std::vector<TextChange>(const Rope &text, const EditorSettings &settings)>;


namespace TextCommands {


std::vector<TextChange> CleanupWhitespace(const Rope &text, const EditorSettings &settings);
std::vector<TextChange> TabsToSpaces(const Rope &text, const EditorSettings &settings);
std::vector<TextChange> LeadingSpacesToTabs(const Rope &text, const EditorSettings &settings);

// Where an offset of the original text is after the changes. An offset in a
// replaced range keeps its distance from the range start, up to the end of the
// replacement; an offset at an insertion stays before the inserted text.
size_t MapOffset(const std::vector<TextChange> &changes, size_t offset);

// Locks the model and makes the changes of the command to its text as one
// undo step owned by owner (CodeEditView::EditOwner()), which restores before
// on undo. Returns the selection after the changes; nothing if the command
// changes nothing.
std::optional<Selection> Execute(
	TextModel &model,
	ObserverId owner,
	const TextCommand &command,
	const EditorSettings &settings,
	const Selection &before
);

// Adds the items of the text commands to the menu and registers the commands
// with EditorCommands.
void AddItems(BMenu *menu);


} // TextCommands


} // CodeEdit
