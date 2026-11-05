#include "FileListView.h"

#include "Messages.h"


namespace GitDiff {


FileItem::FileItem(const std::string &path, DiffKind kind, char letter):
	BStringItem((std::string(1, letter) + "   " + path).c_str()),
	fPath(path),
	fKind(kind)
{
}


FileListView::FileListView():
	BOutlineListView("fileList", B_SINGLE_SELECTION_LIST)
{
	SetSelectionMessage(new BMessage(kMsgFileSelected));
	SetInvocationMessage(new BMessage(kMsgFileInvoked));
}


void FileListView::SetStatus(const std::vector<StatusEntry> &entries)
{
	const std::optional<std::pair<std::string, DiffKind>> selected = Selected();

	std::vector<BListItem*> oldItems;
	for (int32 i = 0; i < FullListCountItems(); i++) {
		oldItems.push_back(FullListItemAt(i));
	}
	MakeEmpty();
	for (BListItem *item : oldItems) {
		delete item;
	}

	fSections[0] = new BStringItem("Unstaged changes");
	fSections[1] = new BStringItem("Staged changes");
	AddItem(fSections[0]);
	for (const StatusEntry &entry : entries) {
		if (entry.conflicted) {
			AddUnder(new FileItem(entry.path, DiffKind::Unstaged, 'U'), fSections[0]);
		} else if (entry.unstaged != ' ') {
			AddUnder(new FileItem(entry.path, DiffKind::Unstaged, entry.unstaged), fSections[0]);
		}
	}
	AddItem(fSections[1]);
	for (const StatusEntry &entry : entries) {
		if (entry.staged != ' ' && !entry.conflicted) {
			AddUnder(new FileItem(entry.path, DiffKind::Staged, entry.staged), fSections[1]);
		}
	}

	if (selected.has_value()) {
		SelectFile(selected->first, selected->second);
	}
}


std::optional<std::pair<std::string, DiffKind>> FileListView::Selected() const
{
	const FileItem *item = dynamic_cast<FileItem*>(ItemAt(CurrentSelection()));
	if (item == nullptr) {
		return std::nullopt;
	}
	return std::pair(item->Path(), item->Kind());
}


bool FileListView::SelectFile(const std::string &path, DiffKind kind)
{
	for (int32 i = 0; i < CountItems(); i++) {
		const FileItem *item = dynamic_cast<FileItem*>(ItemAt(i));
		if (item != nullptr && item->Path() == path && item->Kind() == kind) {
			if (CurrentSelection() != i) {
				Select(i);
			}
			ScrollToSelection();
			return true;
		}
	}
	return false;
}


bool FileListView::SelectFirstFile(DiffKind kind)
{
	BListItem *section = fSections[kind == DiffKind::Unstaged ? 0 : 1];
	if (section == nullptr || CountItemsUnder(section, true) == 0) {
		return false;
	}
	const int32 index = IndexOf(ItemUnderAt(section, true, 0));
	if (index < 0) {
		return false;
	}
	Select(index);
	ScrollToSelection();
	return true;
}


} // GitDiff
