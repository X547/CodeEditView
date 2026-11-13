#include "DirTreeView.h"

#include <stdio.h>
#include <time.h>

#include <ColumnTypes.h>

#include <DiffColors.h>

#include "Messages.h"


namespace DirCompare {


enum {
	kNameColumn,
	kStatusColumn,
	kLeftSizeColumn,
	kLeftTimeColumn,
	kRightSizeColumn,
	kRightTimeColumn,
};


const char *StatusName(EntryStatus status)
{
	switch (status) {
		case EntryStatus::Pending:
			return "Comparing" B_UTF8_ELLIPSIS;
		case EntryStatus::Same:
			return "Identical";
		case EntryStatus::Different:
			return "Different";
		case EntryStatus::LeftOnly:
			return "Left only";
		case EntryStatus::RightOnly:
			return "Right only";
		case EntryStatus::TypeMismatch:
			return "Different types";
		case EntryStatus::Error:
			return "Unreadable";
	}
	return "";
}


namespace {

class NodeRow: public BRow {
public:
	const std::string path;

	explicit NodeRow(const std::string &path): path(path) {}
};


// A text drawn in the colour of an entry status.
class StatusField: public BStringField {
public:
	const EntryStatus status;

	StatusField(const char *text, EntryStatus status):
		BStringField(text),
		status(status)
	{
	}
};


class StatusColumn: public BStringColumn {
public:
	StatusColumn(const char *title, float width, float minWidth, float maxWidth):
		BStringColumn(title, width, minWidth, maxWidth, B_TRUNCATE_END)
	{
	}

	void DrawField(BField *field, BRect rect, BView *parent) override
	{
		if (const StatusField *statusField = dynamic_cast<StatusField*>(field)) {
			const rgb_color text = parent->HighColor();
			switch (statusField->status) {
				case EntryStatus::Pending:
					parent->SetHighColor(mix_color(text, ui_color(B_LIST_BACKGROUND_COLOR), 128));
					break;
				case EntryStatus::Different:
				case EntryStatus::TypeMismatch:
					parent->SetHighColor(DiffKit::kChangeColor);
					break;
				case EntryStatus::LeftOnly:
					parent->SetHighColor(DiffKit::kDeleteColor);
					break;
				case EntryStatus::RightOnly:
					parent->SetHighColor(tint_color(DiffKit::kInsertColor, B_DARKEN_1_TINT));
					break;
				case EntryStatus::Error:
					parent->SetHighColor(200, 110, 0);
					break;
				case EntryStatus::Same:
					break;
			}
		}
		BStringColumn::DrawField(field, rect, parent);
	}
};


std::string FormatSize(const EntryInfo &info)
{
	switch (info.type) {
		case EntryType::File:
			break;
		case EntryType::Symlink:
			return "→ " + info.linkTarget;
		case EntryType::Unreadable:
			return "?";
		default:
			return std::string();
	}
	static const char *kUnits[] = {"KiB", "MiB", "GiB", "TiB"};
	if (info.size < 1024) {
		return std::to_string(info.size) + " B";
	}
	double size = static_cast<double>(info.size) / 1024;
	int unit = 0;
	while (size >= 1024 && unit < 3) {
		size /= 1024;
		unit++;
	}
	char text[32];
	snprintf(text, sizeof(text), "%.1f %s", size, kUnits[unit]);
	return text;
}

}


// localtime_r() is slow for many rows; the text is the same for a minute.
const std::string &DirTreeView::FormatTime(const EntryInfo &info)
{
	static const std::string kNoTime;
	if (info.type == EntryType::None || info.type == EntryType::Unreadable) {
		return kNoTime;
	}
	const time_t seconds = info.modified.tv_sec;
	const time_t minute = seconds >= 0 ? seconds / 60 : (seconds - 59) / 60;
	const auto cached = fTimeTexts.find(minute);
	if (cached != fTimeTexts.end()) {
		return cached->second;
	}
	struct tm local;
	char text[32] = "";
	if (localtime_r(&seconds, &local) != nullptr) {
		strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
	}
	return fTimeTexts[minute] = text;
}


DirTreeView::DirTreeView():
	BColumnListView("tree", B_NAVIGABLE, B_PLAIN_BORDER, true)
{
	SetSortingEnabled(false);
	SetSelectionMode(B_SINGLE_SELECTION_LIST);
	SetSelectionMessage(new BMessage(kMsgEntrySelected));
	SetInvocationMessage(new BMessage(kMsgEntryInvoked));
	AddColumn(new StatusColumn("Name", 220, 60, 2000), kNameColumn);
	AddColumn(new StatusColumn("Status", 90, 40, 300), kStatusColumn);
	AddColumn(new BStringColumn("Left size", 70, 30, 300, B_TRUNCATE_END, B_ALIGN_RIGHT), kLeftSizeColumn);
	AddColumn(new BStringColumn("Left modified", 120, 40, 300, B_TRUNCATE_END), kLeftTimeColumn);
	AddColumn(new BStringColumn("Right size", 70, 30, 300, B_TRUNCATE_END, B_ALIGN_RIGHT), kRightSizeColumn);
	AddColumn(new BStringColumn("Right modified", 120, 40, 300, B_TRUNCATE_END), kRightTimeColumn);
}


bool DirTreeView::IsVisible(const Node &node) const
{
	return fShowIdentical || node.shown != EntryStatus::Same;
}


void DirTreeView::SetFields(BRow *row, const Node &node)
{
	const std::string name = node.IsDirectory() ? node.name + "/" : node.name;
	row->SetField(new StatusField(name.c_str(), node.shown), kNameColumn);
	row->SetField(new StatusField(StatusName(node.shown), node.shown), kStatusColumn);
	row->SetField(new BStringField(FormatSize(node.sides[0]).c_str()), kLeftSizeColumn);
	row->SetField(new BStringField(FormatTime(node.sides[0]).c_str()), kLeftTimeColumn);
	row->SetField(new BStringField(FormatSize(node.sides[1]).c_str()), kRightSizeColumn);
	row->SetField(new BStringField(FormatTime(node.sides[1]).c_str()), kRightTimeColumn);
}


BRow *DirTreeView::CreateRow(const Node &node)
{
	BRow *row = new NodeRow(node.path);
	SetFields(row, node);
	fRows[&node] = row;
	return row;
}


// Expands a directory unless the user collapsed it; directories on one
// side only start collapsed.
void DirTreeView::ExpandDefault(const Node &node)
{
	const auto row = fRows.find(&node);
	if (row == fRows.end() || CountRows(row->second) == 0) {
		return;
	}
	const auto toggled = fToggled.find(node.path);
	const bool expand = toggled != fToggled.end()
		? toggled->second
		: node.sides[0].type == EntryType::Directory && node.sides[1].type == EntryType::Directory;
	if (expand && !row->second->IsExpanded()) {
		fExpanding = true;
		BColumnListView::ExpandOrCollapse(row->second, true);
		fExpanding = false;
	}
}


// Depth first; directories with rows inside go to directories.
void DirTreeView::AddContentRows(const Node &node, BRow *row, std::vector<const Node*> &directories)
{
	for (const std::unique_ptr<Node> &child : node.children) {
		if (!IsVisible(*child)) {
			continue;
		}
		BRow *childRow = CreateRow(*child);
		AddRow(childRow, row);
		if (!child->children.empty()) {
			directories.push_back(child.get());
			AddContentRows(*child, childRow, directories);
		}
	}
}


void DirTreeView::SetComparison(Comparison *comparison)
{
	fComparison = comparison;
	Clear();
	fRows.clear();
	if (fComparison != nullptr) {
		Rebuild();
	}
}


void DirTreeView::SetShowIdentical(bool show)
{
	fShowIdentical = show;
	Rebuild();
}


// BColumnListView walks all rows of expanded directories, from the top, for
// every row it adds and every directory it expands; with many rows that is
// slow. So the rows go in while the top level directories are collapsed, and
// the directories are expanded last, the top level ones from the bottom.
void DirTreeView::Rebuild()
{
	const std::optional<std::string> selected = SelectedPath();
	Clear();
	fRows.clear();
	if (fComparison == nullptr) {
		return;
	}
	std::vector<const Node*> topDirectories;
	BList directoryRows;
	BList fileRows;
	for (const std::unique_ptr<Node> &child : fComparison->Root().children) {
		if (!IsVisible(*child)) {
			continue;
		}
		if (child->IsDirectory()) {
			topDirectories.push_back(child.get());
			directoryRows.AddItem(CreateRow(*child));
		} else {
			fileRows.AddItem(CreateRow(*child));
		}
	}
	// Directories come first.
	AddRows(&directoryRows, -1, nullptr);
	std::vector<const Node*> innerDirectories;
	for (const Node *directory : topDirectories) {
		AddContentRows(*directory, fRows[directory], innerDirectories);
	}
	AddRows(&fileRows, -1, nullptr);

	for (const Node *directory : innerDirectories) {
		ExpandDefault(*directory);
	}
	if (!topDirectories.empty()) {
		// Expanding looks for the focus row too.
		SetFocusRow(fRows[topDirectories.front()], false);
	}
	for (auto it = topDirectories.rbegin(); it != topDirectories.rend(); ++it) {
		ExpandDefault(**it);
	}
	if (selected.has_value()) {
		SelectPath(*selected);
	}
}


std::optional<std::string> DirTreeView::SelectedPath() const
{
	const NodeRow *row = dynamic_cast<const NodeRow*>(CurrentSelection());
	if (row == nullptr) {
		return std::nullopt;
	}
	return row->path;
}


bool DirTreeView::SelectPath(const std::string &path)
{
	if (fComparison == nullptr) {
		return false;
	}
	const Node *node = fComparison->Find(path);
	if (node == nullptr) {
		return false;
	}
	const auto it = fRows.find(node);
	if (it == fRows.end()) {
		return false;
	}
	for (const Node *ancestor = node->parent; ancestor != nullptr; ancestor = ancestor->parent) {
		const auto ancestorRow = fRows.find(ancestor);
		if (ancestorRow != fRows.end() && !ancestorRow->second->IsExpanded()) {
			ExpandOrCollapse(ancestorRow->second, true);
		}
	}
	DeselectAll();
	SetFocusRow(it->second, true);
	ScrollTo(it->second);
	return true;
}


bool DirTreeView::SelectNextDifference(bool next)
{
	if (fComparison == nullptr) {
		return false;
	}
	// Visible entries in tree order.
	std::vector<const Node*> order;
	std::vector<const Node*> stack;
	const Node &root = fComparison->Root();
	for (auto it = root.children.rbegin(); it != root.children.rend(); ++it) {
		stack.push_back(it->get());
	}
	while (!stack.empty()) {
		const Node *node = stack.back();
		stack.pop_back();
		if (!IsVisible(*node)) {
			continue;
		}
		order.push_back(node);
		for (auto it = node->children.rbegin(); it != node->children.rend(); ++it) {
			stack.push_back(it->get());
		}
	}

	const std::optional<std::string> selected = SelectedPath();
	size_t current = order.size();
	for (size_t i = 0; selected.has_value() && i < order.size(); i++) {
		if (order[i]->path == *selected) {
			current = i;
			break;
		}
	}
	auto isTarget = [](const Node *node) {
		return !node->IsDirectory() && node->shown != EntryStatus::Same;
	};
	if (next) {
		for (size_t i = current == order.size() ? 0 : current + 1; i < order.size(); i++) {
			if (isTarget(order[i])) {
				return SelectPath(order[i]->path);
			}
		}
	} else {
		for (size_t i = current; i-- > 0;) {
			if (isTarget(order[i])) {
				return SelectPath(order[i]->path);
			}
		}
	}
	return false;
}


void DirTreeView::ExpandOrCollapse(BRow *row, bool expand)
{
	BColumnListView::ExpandOrCollapse(row, expand);
	const NodeRow *nodeRow = dynamic_cast<const NodeRow*>(row);
	if (!fExpanding && nodeRow != nullptr) {
		fToggled[nodeRow->path] = expand;
	}
}


} // DirCompare
