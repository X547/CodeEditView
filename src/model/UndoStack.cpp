#include "UndoStack.h"

#include <utility>


namespace CodeEdit {


bool UndoStack::CanCoalesce(ObserverId owner, EditKind kind, const Step &step) const
{
	if (!fCanCoalesce || fUndo.empty() || kind == EditKind::Other) {
		return false;
	}
	const Group &top = fUndo.back();
	if (top.owner != owner || top.kind != kind || top.id == fSavedId || top.steps.empty()) {
		return false;
	}

	const Step &last = top.steps.back();
	switch (kind) {
		case EditKind::Typing:
			return step.removed.empty() && step.offset == last.offset + last.inserted.size();
		case EditKind::Backspace:
			return step.inserted.empty() && last.inserted.empty()
				&& step.offset + step.removed.size() == last.offset;
		case EditKind::Delete:
			return step.inserted.empty() && last.inserted.empty() && step.offset == last.offset;
		default:
			break;
	}
	return false;
}


void UndoStack::BeginGroup(std::string_view name, ObserverId owner, EditKind kind, const Selection &before, Step step)
{
	fRedo.clear();
	if (CanCoalesce(owner, kind, step)) {
		fUndo.back().steps.push_back(std::move(step));
		return;
	}

	Group group;
	group.id = fNextId++;
	group.owner = owner;
	group.kind = kind;
	group.name = name;
	group.before = before;
	group.after = before;
	group.steps.push_back(std::move(step));
	fUndo.push_back(std::move(group));
	fCanCoalesce = false;
}


void UndoStack::Append(Step step)
{
	fUndo.back().steps.push_back(std::move(step));
}


void UndoStack::EndGroup(const Selection &after)
{
	fUndo.back().after = after;
	fCanCoalesce = fUndo.back().kind != EditKind::Other;
}


UndoStack::Group UndoStack::PopUndo()
{
	Group group = std::move(fUndo.back());
	fUndo.pop_back();
	fCanCoalesce = false;
	return group;
}


UndoStack::Group UndoStack::PopRedo()
{
	Group group = std::move(fRedo.back());
	fRedo.pop_back();
	fCanCoalesce = false;
	return group;
}


void UndoStack::PushUndo(Group group)
{
	fUndo.push_back(std::move(group));
}


void UndoStack::PushRedo(Group group)
{
	fRedo.push_back(std::move(group));
}


void UndoStack::SetSavePoint()
{
	fSavedId = TopId();
	fCanCoalesce = false;
}


void UndoStack::Clear()
{
	fUndo.clear();
	fRedo.clear();
	fSavedId = 0;
	fCanCoalesce = false;
}


} // CodeEdit
