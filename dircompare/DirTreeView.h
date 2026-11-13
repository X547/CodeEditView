#pragma once

#include <time.h>

#include <map>
#include <optional>
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
	// Modification times as shown, by minute since the epoch.
	std::unordered_map<time_t, std::string> fTimeTexts;

	bool IsVisible(const Node &node) const;
	const std::string &FormatTime(const EntryInfo &info);
	BRow *CreateRow(const Node &node);
	void SetFields(BRow *row, const Node &node);
	void AddContentRows(const Node &node, BRow *row, std::vector<const Node*> &directories);
	void ExpandDefault(const Node &node);

public:
	DirTreeView();

	// Shows a new comparison (none: empty).
	void SetComparison(Comparison *comparison);
	void SetShowIdentical(bool show);

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
