#pragma once

#include <optional>

#include <SupportDefs.h>

#include <CodeEditView/TextModel.h>


class BMenu;


namespace CodeEdit {


struct EditorSettings;


// Text commands: edits of the whole text of the target editor (see
// EditorCommands).
constexpr uint32 kMsgCleanupWhitespace = 'CEws';
constexpr uint32 kMsgTabsToSpaces = 'CEts';
constexpr uint32 kMsgLeadingSpacesToTabs = 'CEst';


namespace TextCommands {


// Each command locks the model and makes its edits as one undo step named
// after it, owned by owner (CodeEditView::EditOwner()), which restores
// selection on undo. It returns the selection after the edits; nothing if
// the text did not change.

// Removes the spaces and tabs at the end of lines and the blank lines at the
// end of the text; a text that is not blank ends with one line break, a blank
// one becomes empty.
std::optional<Selection> CleanupWhitespace(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
);

// Replaces each tab with the spaces up to its tab stop, as the view shows it.
std::optional<Selection> TabsToSpaces(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
);

// Writes the leading whitespace of each line as tabs, keeping its width;
// spaces remain only for a rest narrower than a tab.
std::optional<Selection> LeadingSpacesToTabs(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
);

// Adds the items of the text commands to the menu and registers the commands
// with EditorCommands.
void AddItems(BMenu *menu);


} // TextCommands


} // CodeEdit
