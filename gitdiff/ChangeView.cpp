#include "ChangeView.h"

#include <math.h>

#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuItem.h>
#include <OutlineListView.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringView.h>
#include <Window.h>

#include <CodeEditView/CodeEditView.h>

#include "DiffView.h"
#include "MessageEditor.h"


namespace GitDiff {


using DiffKit::DiffAlgorithm;
using DiffKit::DiffView;


// A file of the list; section titles are plain string items.
class ChangeListItem: public BStringItem {
public:
	std::string key;

	ChangeListItem(const ChangeItem &item):
		BStringItem(item.text.c_str()),
		key(item.key)
	{
	}
};


static bool SameStructure(const std::vector<ChangeSection> &a, const std::vector<ChangeSection> &b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if (a[i].title != b[i].title || a[i].items.size() != b[i].items.size()) {
			return false;
		}
		for (size_t j = 0; j < a[i].items.size(); j++) {
			if (a[i].items[j].key != b[i].items[j].key) {
				return false;
			}
		}
	}
	return true;
}


ChangeView::ChangeView(const char *name, MessageMode messageMode, int messageRows):
	BView(name, 0),
	fMessageMode(messageMode)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fList = new BOutlineListView("files", B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kMsgChangeViewFileSelected));
	fList->SetInvocationMessage(new BMessage(kMsgChangeViewFileInvoked));
	fDiff = new DiffView();

	// Split views ignore preferred sizes; the weights are the initial sizes
	// in pixels for a window about 940 pixels wide.
	fFilesSplit = new BSplitView(B_HORIZONTAL, 0);
	BLayoutBuilder::Split<>(fFilesSplit)
		.Add(new BScrollView("fileScroll", fList, 0, false, true, B_PLAIN_BORDER), 220)
		.Add(fDiff, 720);
	BGroupView *top = new BGroupView(B_VERTICAL, 0);
	fTop = top->GroupLayout();
	fTop->AddView(fFilesSplit);

	if (messageMode == MessageMode::Hidden) {
		BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
			.Add(top);
		return;
	}

	fMessage = CreateMessageEditor("message");
	if (messageMode == MessageMode::ReadOnly) {
		CodeEdit::EditorSettings settings = fMessage->Settings();
		settings.readOnly = true;
		fMessage->SetSettings(settings);
	}
	// The label would limit the width of the panel to its own.
	fMessageLabel = new BStringView("messageLabel", messageMode == MessageMode::ReadOnly ? "Commit:" : "Commit message:");
	fMessageLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	BGroupView *panel = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	fMessagePanel = panel->GroupLayout();
	BLayoutBuilder::Group<>(fMessagePanel)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(fMessageLabel)
		.Add(MessageScrollView("messageScroll", fMessage, messageRows));

	font_height height;
	be_fixed_font->GetHeight(&height);
	const float panelHeight = ceilf(height.ascent + height.descent + height.leading) * messageRows + 70;
	BSplitView *split = new BSplitView(B_VERTICAL, 0);
	BLayoutBuilder::Split<>(split)
		.Add(top, 640 - panelHeight)
		.Add(panel, panelHeight);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(split);
}


void ChangeView::SetMessageHeader(BView *header)
{
	if (fMessagePanel == nullptr) {
		delete header;
		return;
	}
	if (!fMessageLabel->IsHidden()) {
		fMessageLabel->Hide();
	}
	fMessagePanel->AddView(1, header);
}


void ChangeView::SetMessageLabel(const char *text)
{
	if (fMessageLabel != nullptr) {
		fMessageLabel->SetText(text);
	}
}


void ChangeView::SetMessage(const std::string &text)
{
	if (fMessage != nullptr) {
		SetMessageText(fMessage, text);
	}
}


std::string ChangeView::MessageText() const
{
	return fMessage != nullptr ? GitDiff::MessageText(fMessage) : std::string();
}


void ChangeView::ShareTopBorder()
{
	fFilesSplit->SetInsets(0, -1, 0, 0);
}


BGroupLayout *ChangeView::ActionRow()
{
	if (fActions == nullptr) {
		fActions = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		fActions->GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
		fTop->AddView(fActions);
	}
	return fActions->GroupLayout();
}


BGroupLayout *ChangeView::MessageOptionRow()
{
	if (fMessagePanel == nullptr) {
		return nullptr;
	}
	if (fMessageOptions == nullptr) {
		fMessageOptions = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
		fMessagePanel->AddView(fMessageOptions);
	}
	return fMessageOptions->GroupLayout();
}


void ChangeView::SetSections(const std::vector<ChangeSection> &sections)
{
	if (SameStructure(sections, fSections)) {
		// Only texts change, as marks: the items are updated in place, so the
		// selection and scroll position stay.
		int32 index = 0;
		for (const ChangeSection &section : sections) {
			if (!section.title.empty()) {
				index++;
			}
			for (const ChangeItem &item : section.items) {
				BStringItem *listItem = static_cast<BStringItem*>(fList->FullListItemAt(index));
				if (item.text != listItem->Text()) {
					listItem->SetText(item.text.c_str());
					const int32 visible = fList->IndexOf(listItem);
					if (visible >= 0) {
						fList->InvalidateItem(visible);
					}
				}
				index++;
			}
		}
		fSections = sections;
		return;
	}

	const std::optional<std::string> selected = SelectedKey();
	std::vector<BListItem*> oldItems;
	for (int32 i = 0; i < fList->FullListCountItems(); i++) {
		oldItems.push_back(fList->FullListItemAt(i));
	}
	fList->MakeEmpty();
	for (BListItem *item : oldItems) {
		delete item;
	}
	for (const ChangeSection &section : sections) {
		BListItem *title = nullptr;
		if (!section.title.empty()) {
			title = new BStringItem(section.title.c_str());
			fList->AddItem(title);
		}
		if (title != nullptr) {
			// AddUnder() inserts an item as the first under its title.
			for (auto item = section.items.rbegin(); item != section.items.rend(); ++item) {
				fList->AddUnder(new ChangeListItem(*item), title);
			}
		} else {
			for (const ChangeItem &item : section.items) {
				fList->AddItem(new ChangeListItem(item));
			}
		}
	}
	fSections = sections;
	if (selected.has_value()) {
		Select(*selected);
	}
}


std::optional<std::string> ChangeView::SelectedKey() const
{
	const ChangeListItem *item = dynamic_cast<ChangeListItem*>(fList->ItemAt(fList->CurrentSelection()));
	if (item == nullptr) {
		return std::nullopt;
	}
	return item->key;
}


bool ChangeView::Select(const std::string &key)
{
	for (int32 i = 0; i < fList->CountItems(); i++) {
		const ChangeListItem *item = dynamic_cast<ChangeListItem*>(fList->ItemAt(i));
		if (item != nullptr && item->key == key) {
			if (fList->CurrentSelection() != i) {
				fList->Select(i);
			}
			fList->ScrollToSelection();
			return true;
		}
	}
	return false;
}


bool ChangeView::SelectFirst(size_t section)
{
	// Without titles, all files are in section 0.
	int32 current = fList->CountItems() > 0 && dynamic_cast<ChangeListItem*>(fList->ItemAt(0)) == nullptr ? -1 : 0;
	for (int32 i = 0; i < fList->CountItems(); i++) {
		if (dynamic_cast<ChangeListItem*>(fList->ItemAt(i)) == nullptr) {
			current++;
		} else if (current == static_cast<int32>(section)) {
			fList->Select(i);
			fList->ScrollToSelection();
			return true;
		}
	}
	return false;
}


void ChangeView::AddViewItems(BMenu *menu)
{
	fSyncScrollingItem = new BMenuItem("Synchronize scrolling", new BMessage(kMsgChangeViewSyncScrolling));
	fSyncScrollingItem->SetMarked(true);
	fHideUnchangedItem = new BMenuItem("Hide unchanged lines", new BMessage(kMsgChangeViewHideUnchanged));
	fHideUnchangedItem->SetMarked(true);
	fWordWrapItem = new BMenuItem("Word wrap", new BMessage(kMsgChangeViewWordWrap));
	menu->AddItem(fSyncScrollingItem);
	menu->AddItem(fHideUnchangedItem);
	menu->AddItem(fWordWrapItem);
	menu->AddSeparatorItem();
	BMenu *algorithmMenu = new BMenu("Diff algorithm");
	algorithmMenu->SetRadioMode(true);
	static const char *kAlgorithmNames[] = {"Myers", "Minimal", "Patience"};
	for (int i = 0; i < 3; i++) {
		BMessage *message = new BMessage(kMsgChangeViewAlgorithm);
		message->AddInt32("algorithm", i);
		fAlgorithmItems[i] = new BMenuItem(kAlgorithmNames[i], message);
		algorithmMenu->AddItem(fAlgorithmItems[i]);
	}
	fAlgorithmItems[0]->SetMarked(true);
	menu->AddItem(algorithmMenu);
}


void ChangeView::AddNavigationItems(BMenu *menu)
{
	menu->AddItem(new BMenuItem("Next change", new BMessage(kMsgChangeViewNextChange), B_DOWN_ARROW, B_SHIFT_KEY));
	menu->AddItem(new BMenuItem("Previous change", new BMessage(kMsgChangeViewPreviousChange), B_UP_ARROW,
		B_SHIFT_KEY));
}


// Toggles the mark of an item, which may not exist; returns the new state.
static bool Toggle(BMenuItem *item, bool &state)
{
	state = !state;
	if (item != nullptr) {
		item->SetMarked(state);
	}
	return state;
}


bool ChangeView::HandleCommand(BMessage *message)
{
	switch (message->what) {
		case kMsgChangeViewSyncScrolling:
			fDiff->SetSyncScrolling(Toggle(fSyncScrollingItem, fSyncScrolling));
			return true;
		case kMsgChangeViewHideUnchanged:
			fDiff->SetHideUnchanged(Toggle(fHideUnchangedItem, fHideUnchanged));
			return true;
		case kMsgChangeViewWordWrap:
			fDiff->SetWordWrap(Toggle(fWordWrapItem, fWordWrap));
			return true;
		case kMsgChangeViewAlgorithm: {
			const int32 algorithm = message->GetInt32("algorithm", 0);
			if (algorithm >= 0 && algorithm < 3) {
				fDiff->SetAlgorithm(static_cast<DiffAlgorithm>(algorithm));
				if (fAlgorithmItems[algorithm] != nullptr) {
					fAlgorithmItems[algorithm]->SetMarked(true);
				}
			}
			return true;
		}
		case kMsgChangeViewNextChange:
		case kMsgChangeViewPreviousChange:
			fDiff->GoToChunk(message->what == kMsgChangeViewNextChange);
			return true;
		default:
			return false;
	}
}


void ChangeView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (fMessage != nullptr) {
		fMessage->SetTarget(BMessenger(this));
	}
}


void ChangeView::MessageReceived(BMessage *message)
{
	if (message->what == CodeEdit::kMsgContentsChanged) {
		if (fMessageMode == MessageMode::Editable) {
			UpdateMessageDecorations(fMessage);
		}
		return;
	}
	BView::MessageReceived(message);
}


} // GitDiff
