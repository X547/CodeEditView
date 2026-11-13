#pragma once

#include <stddef.h>
#include <stdint.h>

#include <optional>
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


// Maps model lines to display rows: a line has as many rows as it wraps
// into, and none while hidden by a fold. Row counts are set by the owner; a
// line marked stale keeps its old count until it is wrapped again.
//
// While every line has one row (no wrapping, no folds) the map is the
// identity and keeps nothing per line; it keeps line states from the first
// line with another count or a stale mark until Reset().
class DisplayMap {
private:
	struct LineState {
		uint32_t wrapRows: 30 = 1;
		uint32_t hidden: 1 = 0;
		uint32_t stale: 1 = 0;
	};

	// Of the identity map.
	size_t fLineCount {};
	bool fDense {};
	std::vector<LineState> fLines;
	RowIndex fRows;
	size_t fStaleCount {};
	size_t fStaleCursor {};

	void MakeDense();
	void UpdateRows(size_t line);

public:
	void Reset(size_t lineCount);
	// Replaced lines get one row each and are not hidden or stale.
	void ApplyEdit(const EditRecord &edit);
	// Inclusive line ranges without display rows (folded lines).
	void SetHiddenLines(const std::vector<std::pair<size_t, size_t>> &ranges);
	bool IsHidden(size_t line) const {return fDense && fLines[line].hidden;}

	uint32_t WrapRows(size_t line) const {return fDense ? fLines[line].wrapRows : 1;}
	// Also clears the stale mark.
	void SetWrapRows(size_t line, uint32_t rows);
	// Marks every line for re-wrapping; NextStaleLine() starts at line.
	void MarkAllStale(size_t line = 0);
	bool HasStaleLines() const {return fStaleCount > 0;}
	bool IsStale(size_t line) const {return fDense && fLines[line].stale;}
	std::optional<size_t> NextStaleLine();

	// A state is kept for each line.
	bool IsDense() const {return fDense;}
	size_t LineCount() const {return fDense ? fRows.LineCount() : fLineCount;}
	size_t RowCount() const {return fDense ? fRows.TotalRows() : fLineCount;}
	DisplayRow RowAt(size_t row) const;
	size_t FirstRowOfLine(size_t line) const {return fDense ? fRows.FirstRow(line) : line;}
};


} // CodeEdit
