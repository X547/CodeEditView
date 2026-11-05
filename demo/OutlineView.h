#pragma once

#include <stddef.h>

#include <string>
#include <vector>

#include <ListItem.h>
#include <OutlineListView.h>

#include <CodeEditView/Outline.h>


static constexpr uint32 kMsgOutlineSelected = 'Dosl';
static constexpr uint32 kMsgOutlineInvoked = 'Doiv';


class OutlineListItem: public BListItem {
private:
	CodeEdit::OutlineKind fKind;
	std::string fLabel;
	size_t fStart;
	size_t fEnd;
	size_t fNameStart;
	size_t fNameEnd;
	// Identifies the item across rebuilds for keeping it collapsed.
	std::string fPath;

	friend class OutlineView;

public:
	OutlineListItem(const CodeEdit::OutlineItem &item, uint32 level, bool expanded, const std::string &path);

	size_t Start() const {return fStart;}
	size_t End() const {return fEnd;}
	size_t NameStart() const {return fNameStart;}
	size_t NameEnd() const {return fNameEnd;}

	void DrawItem(BView *owner, BRect frame, bool complete) override;
	void Update(BView *owner, const BFont *font) override;
};


// Definitions of a document as a tree. Selecting an item sends
// kMsgOutlineSelected, Enter or double click kMsgOutlineInvoked.
class OutlineView: public BOutlineListView {
private:
	void Clear();

public:
	OutlineView();
	~OutlineView() override;

	void MouseDown(BPoint where) override;

	// Rebuilds the list if the definitions differ, keeping collapsed items
	// and the scroll position; otherwise only updates their offsets.
	void SetOutline(const std::vector<CodeEdit::OutlineItem> &items);
	// Selects the innermost visible definition containing offset, without
	// sending kMsgOutlineSelected.
	void SelectAt(size_t offset);
	OutlineListItem *SelectedItem() const;
};
