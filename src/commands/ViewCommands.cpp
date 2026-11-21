#include "EditCommands.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <InterfaceDefs.h>
#include <Looper.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>

#include <CodeEditView/CodeEditView.h>


namespace CodeEdit {


namespace EditorCommands {


// Lines of the bookmarks of a model, sorted. Requires the model lock.
static std::vector<size_t> BookmarkLines(const TextModel &model)
{
	std::vector<size_t> lines;
	for (const MarkInfo &mark : model.SharedMarks().Marks(0, model.Text().NewlineCount())) {
		if (mark.kind == kMarkBookmark) {
			lines.push_back(mark.line);
		}
	}
	return lines;
}


static void ToggleBookmark(CodeEditView &view)
{
	const size_t line = view.CaretLocation().line;
	TextModel &model = *view.Model();
	ModelLock lock(model);
	MarkSet &marks = model.SharedMarks();
	bool removed = false;
	for (const MarkInfo &mark : marks.Marks(line, line)) {
		if (mark.kind == kMarkBookmark) {
			marks.RemoveMark(mark.id);
			removed = true;
		}
	}
	if (!removed) {
		// A bookmark stays when its line is joined with the one before.
		marks.AddMark(kMarkBookmark, line, 0, MarkDeletePolicy::MoveToEditLine);
	}
}


// Wraps around at the ends.
static void GoToBookmark(CodeEditView &view, bool forward)
{
	const size_t caretLine = view.CaretLocation().line;
	std::optional<size_t> target;
	{
		TextModel &model = *view.Model();
		ModelLock lock(model);
		const std::vector<size_t> lines = BookmarkLines(model);
		if (lines.empty()) {
			return;
		}
		if (forward) {
			const auto next = std::upper_bound(lines.begin(), lines.end(), caretLine);
			target = next != lines.end() ? *next : lines.front();
		} else {
			const auto previous = std::lower_bound(lines.begin(), lines.end(), caretLine);
			target = previous != lines.begin() ? *(previous - 1) : lines.back();
		}
	}
	view.GoToLocation({*target, 0});
}


static bool HasBookmarks(CodeEditView &view)
{
	TextModel &model = *view.Model();
	ModelLock lock(model);
	return !BookmarkLines(model).empty();
}


// Shift, folding and bookmarks. The view executes shift and folding itself.
class ViewCommandSet: public EditorCommandSet {
public:
	bool Handles(uint32 what) const final;
	bool IsEnabled(uint32 what, CodeEditView *view) final;
	void Execute(BMessage *message, CodeEditView *view) final;
};


bool ViewCommandSet::Handles(uint32 what) const
{
	switch (what) {
		case kMsgIndentLess:
		case kMsgIndentMore:
		case kMsgToggleFold:
		case kMsgFoldAll:
		case kMsgUnfoldAll:
		case kMsgToggleBookmark:
		case kMsgNextBookmark:
		case kMsgPreviousBookmark:
		case kMsgClearBookmarks:
			return true;
		default:
			return false;
	}
}


bool ViewCommandSet::IsEnabled(uint32 what, CodeEditView *view)
{
	if (view == nullptr) {
		return false;
	}
	switch (what) {
		case kMsgIndentLess:
		case kMsgIndentMore:
			return !view->Settings().readOnly;
		case kMsgNextBookmark:
		case kMsgPreviousBookmark:
		case kMsgClearBookmarks:
			return HasBookmarks(*view);
		default:
			return true;
	}
}


void ViewCommandSet::Execute(BMessage *message, CodeEditView *view)
{
	if (view == nullptr) {
		return;
	}
	switch (message->what) {
		case kMsgToggleBookmark:
			ToggleBookmark(*view);
			break;
		case kMsgNextBookmark:
		case kMsgPreviousBookmark:
			GoToBookmark(*view, message->what == kMsgNextBookmark);
			break;
		case kMsgClearBookmarks: {
			TextModel &model = *view->Model();
			ModelLock lock(model);
			model.SharedMarks().RemoveMarks(kMarkBookmark);
			break;
		}
		default:
			if (view->Looper() != nullptr) {
				view->Looper()->PostMessage(message, view);
			}
			break;
	}
}


EditorCommandSet &BuiltInViewCommands()
{
	static ViewCommandSet sCommands;
	return sCommands;
}


void AddShiftItems(BMenu *menu)
{
	menu->AddItem(new BMenuItem("Shift left", new BMessage(kMsgIndentLess), '['));
	menu->AddItem(new BMenuItem("Shift right", new BMessage(kMsgIndentMore), ']'));
}


void AddFoldItems(BMenu *menu)
{
	menu->AddItem(new BMenuItem("Fold/unfold", new BMessage(kMsgToggleFold), '.'));
	menu->AddItem(new BMenuItem("Fold all", new BMessage(kMsgFoldAll)));
	menu->AddItem(new BMenuItem("Unfold all", new BMessage(kMsgUnfoldAll)));
}


void AddBookmarkItems(BMenu *menu)
{
	menu->AddItem(new BMenuItem("Toggle bookmark", new BMessage(kMsgToggleBookmark), 'M'));
	menu->AddItem(new BMenuItem("Next bookmark", new BMessage(kMsgNextBookmark), B_DOWN_ARROW, B_OPTION_KEY));
	menu->AddItem(new BMenuItem("Previous bookmark", new BMessage(kMsgPreviousBookmark), B_UP_ARROW, B_OPTION_KEY));
	menu->AddItem(new BMenuItem("Remove all bookmarks", new BMessage(kMsgClearBookmarks)));
}


} // EditorCommands


} // CodeEdit
