#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <ColumnListView.h>

#include "Comparison.h"


namespace DirCompare {


// The merged tree of a Comparison: one row per entry, with its status and
// the size and time on each side. Rows are coloured by status. Unless
// identical entries are shown, only entries that differ are listed.
class DirTreeView: public BColumnListView {
private:
	Comparison *fComparison {};
	bool fShowIdentical {};
	std::unordered_map<const Node*, BRow*> fRows;
	// Directories the user expanded (true) or collapsed (false).
	std::map<std::string, bool> fToggled;
	bool fExpanding {};

	bool IsVisible(const Node &node) const;
	BRow *CreateRow(const Node &node);
	void SetFields(BRow *row, const Node &node);
	void AddChildRows(const Node &parent);
	void AddSubtree(const Node &node, BRow *parentRow);
	void ExpandDefault(const Node &node);
	void RemoveSubtree(const Node &node);
	BRow *ParentRow(const Node &node) const;

public:
	DirTreeView();

	// Shows a new comparison (none: empty).
	void SetComparison(Comparison *comparison);
	void SetShowIdentical(bool show);

	// Follows the comparison after scan results: rows for new nodes, and
	// changed statuses (rows of entries that became identical go).
	void Update(const std::vector<Node*> &added, const std::vector<Node*> &changed);
	// Recreates all rows, e.g. after nodes were replaced; keeps the
	// selection.
	void Rebuild();

	// The path of the selected entry, if any.
	std::optional<std::string> SelectedPath() const;
	bool SelectPath(const std::string &path);
	// Selects the next or previous different file in tree order, expanding
	// its directories; returns whether there was one.
	bool SelectNextDifference(bool next);

	void ExpandOrCollapse(BRow *row, bool expand) override;
};


// The name of a status as shown in the tree.
const char *StatusName(EntryStatus status);


} // DirCompare
