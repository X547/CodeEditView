#pragma once

#include <stddef.h>
#include <stdint.h>

#include <utility>
#include <vector>


namespace CodeEdit {


// Number of display rows per model line with prefix sums (Fenwick tree,
// rebuilt lazily after lines are inserted or removed). A line may have zero
// rows (hidden by a fold). There are fewer than 2^32 rows.
class RowIndex {
private:
	std::vector<uint32_t> fRows;
	mutable std::vector<uint32_t> fTree;
	mutable bool fTreeValid {};
	size_t fTotal {};

	void BuildTree() const;

public:
	void Reset(size_t lineCount, uint32_t rowsPerLine = 1);
	// Replaces oldCount lines starting at first with newCount lines.
	void ReplaceLines(size_t first, size_t oldCount, size_t newCount, uint32_t rowsPerLine = 1);
	void SetRows(size_t line, uint32_t rows);

	uint32_t Rows(size_t line) const {return fRows[line];}
	size_t LineCount() const {return fRows.size();}
	size_t TotalRows() const {return fTotal;}

	// Rows before line; line may be LineCount().
	size_t FirstRow(size_t line) const;
	// Line containing row (< TotalRows()) and the row index within the line.
	std::pair<size_t, uint32_t> LineOfRow(size_t row) const;
};


} // CodeEdit
