#pragma once

#include <stddef.h>
#include <stdint.h>

#include "RowIndex.h"


namespace CodeEdit {


struct EditRecord;


struct DisplayRow {
	size_t line {};
	// Row within the line (continuation rows of a wrapped line).
	uint32_t subRow {};
};


// Maps model lines to display rows. Currently one row per line; word wrap and
// folds change the per-line row counts.
class DisplayMap {
private:
	RowIndex fRows;

public:
	void Reset(size_t lineCount);
	void ApplyEdit(const EditRecord &edit);

	size_t LineCount() const {return fRows.LineCount();}
	size_t RowCount() const {return fRows.TotalRows();}
	DisplayRow RowAt(size_t row) const;
	size_t FirstRowOfLine(size_t line) const {return fRows.FirstRow(line);}
};


} // CodeEdit
