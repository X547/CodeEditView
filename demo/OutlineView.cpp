#include "OutlineView.h"

#include <math.h>

#include <algorithm>
#include <set>

#include <ControlLook.h>
#include <Font.h>


using CodeEdit::OutlineItem;
using CodeEdit::OutlineKind;


struct KindStyle {
	const char *letter;
	rgb_color color;
};


static KindStyle StyleOfKind(OutlineKind kind)
{
	switch (kind) {
		case OutlineKind::Namespace:
			return {"N", {120, 120, 120, 255}};
		case OutlineKind::Class:
			return {"C", {40, 110, 200, 255}};
		case OutlineKind::Struct:
			return {"S", {40, 150, 150, 255}};
		case OutlineKind::Union:
			return {"U", {130, 90, 180, 255}};
		case OutlineKind::Enum:
			return {"E", {200, 130, 30, 255}};
		case OutlineKind::Function:
		default:
			return {"f", {170, 60, 140, 255}};
	}
}


//#pragma mark - OutlineListItem

OutlineListItem::OutlineListItem(const OutlineItem &item, uint32 level, bool expanded, const std::string &path):
	BListItem(level, expanded),
	fKind(item.kind),
	fLabel(item.label),
	fStart(item.start),
	fEnd(item.end),
	fNameStart(item.nameStart),
	fNameEnd(item.nameEnd),
	fPath(path)
{
	// BListView skips disabled items in keyboard navigation and never
	// selects them.
	SetEnabled(fKind != OutlineKind::Separator);
}


void OutlineListItem::Update(BView *owner, const BFont *font)
{
	BListItem::Update(owner, font);
	if (fKind == OutlineKind::Separator) {
		SetHeight(7);
		SetWidth(0);
		return;
	}
	font_height height;
	font->GetHeight(&height);
	SetHeight(ceilf(height.ascent + height.descent + height.leading) + 4);
	if (fKind == OutlineKind::Section) {
		BFont bold(font);
		bold.SetFace(B_BOLD_FACE);
		SetWidth(bold.StringWidth(fLabel.c_str()) + 6);
		return;
	}
	SetWidth(font->StringWidth(fLabel.c_str()) + Height() + 8);
}


void OutlineListItem::DrawItem(BView *owner, BRect frame, bool complete)
{
	const rgb_color background = IsSelected()
		? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR)
		: ui_color(B_LIST_BACKGROUND_COLOR);
	if (IsSelected() || complete) {
		owner->SetHighColor(background);
		owner->FillRect(frame);
	}

	if (fKind == OutlineKind::Separator) {
		const float y = floorf(frame.top + frame.Height() / 2);
		owner->SetHighColor(tint_color(background, B_DARKEN_2_TINT));
		owner->StrokeLine(BPoint(frame.left + 2, y), BPoint(frame.right - 2, y));
		return;
	}

	font_height height;
	owner->GetFontHeight(&height);
	const float baseline = floorf(frame.top + (frame.Height() + 1 + height.ascent - height.descent) / 2);

	if (fKind == OutlineKind::Section) {
		BFont font;
		owner->GetFont(&font);
		BFont bold(font);
		bold.SetFace(B_BOLD_FACE);
		owner->SetFont(&bold, B_FONT_FACE);
		owner->SetHighColor(ui_color(IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR));
		owner->SetLowColor(background);
		owner->DrawString(fLabel.c_str(), BPoint(frame.left + 2, baseline));
		owner->SetFont(&font, B_FONT_FACE);
		return;
	}

	// A colored badge with a letter for the kind.
	const KindStyle style = StyleOfKind(fKind);
	const float badgeSize = std::max(8.0f, frame.Height() - 5);
	const BRect badge(
		frame.left + 2, floorf(frame.top + (frame.Height() - badgeSize) / 2),
		frame.left + 2 + badgeSize, floorf(frame.top + (frame.Height() - badgeSize) / 2) + badgeSize
	);
	owner->SetHighColor(style.color);
	owner->FillRoundRect(badge, 2, 2);
	owner->SetHighColor(255, 255, 255);
	owner->SetLowColor(style.color);
	owner->DrawString(style.letter, BPoint(
		badge.left + (badge.Width() + 1 - owner->StringWidth(style.letter)) / 2,
		baseline
	));

	owner->SetHighColor(ui_color(IsSelected() ? B_LIST_SELECTED_ITEM_TEXT_COLOR : B_LIST_ITEM_TEXT_COLOR));
	owner->SetLowColor(background);
	owner->DrawString(fLabel.c_str(), BPoint(badge.right + 5, baseline));
}


//#pragma mark - OutlineView

OutlineView::OutlineView():
	BOutlineListView("outline", B_SINGLE_SELECTION_LIST)
{
	SetSelectionMessage(new BMessage(kMsgOutlineSelected));
	SetInvocationMessage(new BMessage(kMsgOutlineInvoked));
}


OutlineView::~OutlineView()
{
	Clear();
}


void OutlineView::MouseDown(BPoint where)
{
	// BListView deselects all on a click on a disabled item.
	const BListItem *item = ItemAt(IndexOf(where));
	if (item != nullptr && !item->IsEnabled()) {
		MakeFocus(true);
		return;
	}
	BOutlineListView::MouseDown(where);
}


void OutlineView::Clear()
{
	std::vector<BListItem*> items;
	for (int32 i = 0; i < FullListCountItems(); i++) {
		items.push_back(FullListItemAt(i));
	}
	MakeEmpty();
	for (BListItem *item : items) {
		delete item;
	}
}


struct FlatItem {
	const OutlineItem *item;
	uint32 level;
	// Labels from the top level down, identifying the item across rebuilds.
	// Section labels are left out of their children's paths, so adding or
	// renaming a mark does not expand the items below it.
	std::string path;
};


static void Flatten(const std::vector<OutlineItem> &items, uint32 level, const std::string &parentPath, std::vector<FlatItem> &flat)
{
	for (const OutlineItem &item : items) {
		const bool section = item.kind == OutlineKind::Section;
		std::string path = parentPath + (section ? "\n#" : "\n") + item.label;
		flat.push_back({&item, level, path});
		Flatten(item.children, level + 1, section ? parentPath : path, flat);
	}
}


void OutlineView::SetOutline(const std::vector<OutlineItem> &items)
{
	std::vector<FlatItem> flat;
	Flatten(items, 0, std::string(), flat);

	// The same definitions: only the offsets changed (e.g. by typing).
	bool same = flat.size() == static_cast<size_t>(FullListCountItems());
	for (size_t i = 0; same && i < flat.size(); i++) {
		const OutlineListItem *existing = static_cast<OutlineListItem*>(FullListItemAt(i));
		same = existing->OutlineLevel() == flat[i].level && existing->fKind == flat[i].item->kind
			&& existing->fLabel == flat[i].item->label;
	}
	if (same) {
		for (size_t i = 0; i < flat.size(); i++) {
			OutlineListItem *existing = static_cast<OutlineListItem*>(FullListItemAt(i));
			existing->fStart = flat[i].item->start;
			existing->fEnd = flat[i].item->end;
			existing->fNameStart = flat[i].item->nameStart;
			existing->fNameEnd = flat[i].item->nameEnd;
		}
		return;
	}

	std::set<std::string> collapsed;
	for (int32 i = 0; i < FullListCountItems(); i++) {
		const OutlineListItem *existing = static_cast<OutlineListItem*>(FullListItemAt(i));
		if (!existing->IsExpanded()) {
			collapsed.insert(existing->fPath);
		}
	}

	const BPoint scrollPosition = Bounds().LeftTop();
	BMessage *selectionMessage = SelectionMessage() != nullptr ? new BMessage(*SelectionMessage()) : nullptr;
	SetSelectionMessage(nullptr);
	Clear();
	for (const FlatItem &entry : flat) {
		AddItem(new OutlineListItem(*entry.item, entry.level, collapsed.count(entry.path) == 0, entry.path));
	}
	ScrollTo(scrollPosition);
	SetSelectionMessage(selectionMessage);
}


void OutlineView::SelectAt(size_t offset)
{
	// Items are in document order; the last one containing offset among the
	// visible ones is the innermost.
	int32 found = -1;
	for (int32 i = 0; i < CountItems(); i++) {
		const OutlineListItem *item = static_cast<OutlineListItem*>(ItemAt(i));
		if (item->fStart <= offset && offset < item->fEnd) {
			found = i;
		}
	}
	if (found == CurrentSelection()) {
		return;
	}
	BMessage *selectionMessage = SelectionMessage() != nullptr ? new BMessage(*SelectionMessage()) : nullptr;
	SetSelectionMessage(nullptr);
	if (found < 0) {
		DeselectAll();
	} else {
		Select(found);
		ScrollToSelection();
	}
	SetSelectionMessage(selectionMessage);
}


OutlineListItem *OutlineView::SelectedItem() const
{
	const int32 index = CurrentSelection();
	return index >= 0 ? static_cast<OutlineListItem*>(ItemAt(index)) : nullptr;
}
