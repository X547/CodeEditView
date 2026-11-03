#pragma once

#include <stddef.h>
#include <stdint.h>

#include <utility>
#include <vector>

#include "RowIndex.h"


namespace CodeEdit {


struct EditRecord;


struct DisplayRow {
	size_t line {};
	// Row within the line (continuation rows of a wrapped line).
	uint32_t subRow {};
};


// Maps model lines to display rows: one row per line, none for lines hidden
// by folds. Word wrap will add continuation rows.
class DisplayMap {
private:
	RowIndex fRows;

public:
	void Reset(size_t lineCount);
	void ApplyEdit(const EditRecord &edit);
	// Inclusive line ranges without display rows (folded lines).
	void SetHiddenLines(const std::vector<std::pair<size_t, size_t>> &ranges);

	size_t LineCount() const {return fRows.LineCount();}
	size_t RowCount() const {return fRows.TotalRows();}
	DisplayRow RowAt(size_t row) const;
	size_t FirstRowOfLine(size_t line) const {return fRows.FirstRow(line);}
};


} // CodeEdit
