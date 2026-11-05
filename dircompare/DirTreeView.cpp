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


std::string FormatTime(const EntryInfo &info)
{
	if (info.type == EntryType::None || info.type == EntryType::Unreadable) {
		return std::string();
	}
	struct tm local;
	const time_t seconds = info.modified.tv_sec;
	if (localtime_r(&seconds, &local) == nullptr) {
		return std::string();
	}
	char text[32];
	strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
	return text;
}

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


BRow *DirTreeView::ParentRow(const Node &node) const
{
	if (node.parent == nullptr || node.parent->parent == nullptr) {
		return nullptr;
	}
	const auto it = fRows.find(node.parent);
	return it != fRows.end() ? it->second : nullptr;
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


void DirTreeView::AddChildRows(const Node &parent)
{
	BRow *parentRow = nullptr;
	if (parent.parent != nullptr) {
		const auto it = fRows.find(&parent);
		if (it == fRows.end()) {
			return;
		}
		parentRow = it->second;
	}
	for (const std::unique_ptr<Node> &child : parent.children) {
		if (IsVisible(*child) && fRows.count(child.get()) == 0) {
			AddRow(CreateRow(*child), parentRow);
		}
	}
	ExpandDefault(parent);
}


void DirTreeView::AddSubtree(const Node &node, BRow *parentRow)
{
	if (!IsVisible(node)) {
		return;
	}
	BRow *row = CreateRow(node);
	AddRow(row, parentRow);
	for (const std::unique_ptr<Node> &child : node.children) {
		AddSubtree(*child, row);
	}
	ExpandDefault(node);
}


void DirTreeView::RemoveSubtree(const Node &node)
{
	for (const std::unique_ptr<Node> &child : node.children) {
		RemoveSubtree(*child);
	}
	const auto it = fRows.find(&node);
	if (it != fRows.end()) {
		RemoveRow(it->second);
		delete it->second;
		fRows.erase(it);
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


void DirTreeView::Update(const std::vector<Node*> &added, const std::vector<Node*> &changed)
{
	if (fComparison == nullptr) {
		return;
	}
	// The entries of a directory come together, so rows are added per
	// parent in the order of the nodes.
	std::set<const Node*> parents;
	for (const Node *node : added) {
		if (node->parent != nullptr && parents.insert(node->parent).second) {
			AddChildRows(*node->parent);
		}
	}
	for (const Node *node : changed) {
		if (node->parent == nullptr) {
			continue;
		}
		const auto it = fRows.find(node);
		if (it == fRows.end()) {
			if (IsVisible(*node) && (node->parent->parent == nullptr || fRows.count(node->parent) != 0)) {
				AddSubtree(*node, ParentRow(*node));
			}
		} else if (!IsVisible(*node)) {
			RemoveSubtree(*node);
		} else {
			SetFields(it->second, *node);
			UpdateRow(it->second);
		}
	}
}


void DirTreeView::Rebuild()
{
	const std::optional<std::string> selected = SelectedPath();
	Clear();
	fRows.clear();
	if (fComparison == nullptr) {
		return;
	}
	for (const std::unique_ptr<Node> &child : fComparison->Root().children) {
		AddSubtree(*child, nullptr);
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
