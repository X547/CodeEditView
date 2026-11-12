#pragma once

#include <optional>
#include <string>
#include <vector>

#include <View.h>


class BGroupLayout;
class BGroupView;
class BMenu;
class BMenuItem;
class BOutlineListView;
class BSplitView;
class BStringView;
class CodeEditView;


namespace DiffKit {
class DiffView;
}


namespace GitDiff {


// Commands of the menu items that ChangeView adds; the window passes them
// to HandleCommand().
constexpr uint32 kMsgChangeViewSyncScrolling = 'CVss';
constexpr uint32 kMsgChangeViewHideUnchanged = 'CVhu';
constexpr uint32 kMsgChangeViewWordWrap = 'CVww';
// "algorithm" (int32): DiffAlgorithm.
constexpr uint32 kMsgChangeViewAlgorithm = 'CVal';
constexpr uint32 kMsgChangeViewNextChange = 'CVnc';
constexpr uint32 kMsgChangeViewPreviousChange = 'CVpc';

// Posted to the window when a file of the list is selected or invoked
// (double-clicked); ChangeView::SelectedKey() says which.
constexpr uint32 kMsgChangeViewFileSelected = 'CVfs';
constexpr uint32 kMsgChangeViewFileInvoked = 'CVfi';


// Whether and how the commit message is shown.
enum class MessageMode {
	Hidden,
	ReadOnly,
	Editable,
};


struct ChangeItem {
	// Identifies the file for the host.
	std::string key;
	std::string text;
};


// A titled group of files; with an empty title the files are not grouped.
struct ChangeSection {
	std::string title;
	std::vector<ChangeItem> items;
};


// The parts shared by the windows that show changes of files: a list of the
// files, the diff of the selected one, an optional row of actions under the
// diff, and the commit message (hidden, read-only or editable) with an
// optional row of options under it. The host fills the list, shows the
// models of the selected file in Diff(), and adds its own controls to the
// rows.
class ChangeView: public BView {
private:
	MessageMode fMessageMode;
	BOutlineListView *fList {};
	BSplitView *fFilesSplit {};
	DiffKit::DiffView *fDiff {};
	CodeEditView *fMessage {};
	BStringView *fMessageLabel {};
	BGroupLayout *fTop {};
	BGroupLayout *fMessagePanel {};
	BGroupView *fActions {};
	BGroupView *fMessageOptions {};
	BMenuItem *fSyncScrollingItem {};
	BMenuItem *fHideUnchangedItem {};
	BMenuItem *fWordWrapItem {};
	BMenuItem *fAlgorithmItems[3] {};
	bool fSyncScrolling = true;
	bool fHideUnchanged = true;
	bool fWordWrap = false;
	// What the list shows, to update it in place when only texts change.
	std::vector<ChangeSection> fSections;

public:
	// messageRows: the height of the message editor in lines.
	ChangeView(const char *name, MessageMode messageMode, int messageRows = 8);

	DiffKit::DiffView *Diff() const {return fDiff;}
	// None if the message is hidden.
	CodeEditView *Message() const {return fMessage;}
	MessageMode GetMessageMode() const {return fMessageMode;}
	void SetMessageLabel(const char *text);
	// A view above the message in place of its label, e.g. the details of a
	// commit; the view is owned.
	void SetMessageHeader(BView *header);
	// Sets an editable or read-only message, with its line checks.
	void SetMessage(const std::string &text);
	std::string MessageText() const;

	// For a view right below a border (as a menu bar's): the list and the
	// panes start one pixel higher, so their top frame lines are hidden under
	// it, while the splitters start below it.
	void ShareTopBorder();

	// Rows for the host's controls, created when first asked for: under the
	// diff, and under the message.
	BGroupLayout *ActionRow();
	BGroupLayout *MessageOptionRow();

	// Rebuilds the list unless only the texts changed, keeping the selected
	// file if it is still listed.
	void SetSections(const std::vector<ChangeSection> &sections);
	std::optional<std::string> SelectedKey() const;
	// Selects a file; returns whether it is listed.
	bool Select(const std::string &key);
	// Selects the first file of a section, if any.
	bool SelectFirst(size_t section = 0);

	// Menu items for the diff: synchronized scrolling, hiding unchanged
	// lines, word wrap and the diff algorithm; next and previous change.
	void AddViewItems(BMenu *menu);
	void AddNavigationItems(BMenu *menu);
	// Handles the commands of those items, and passes editing commands
	// (undo, copy etc.) to the focused editor. Returns whether it handled
	// the message.
	bool HandleCommand(BMessage *message);

	void AttachedToWindow() override;
	void MessageReceived(BMessage *message) override;
};


} // GitDiff
