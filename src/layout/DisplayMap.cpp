#include "DisplayMap.h"

#include <CodeEditView/EditRecord.h>


namespace CodeEdit {


void DisplayMap::Reset(size_t lineCount)
{
	fRows.Reset(lineCount);
}


void DisplayMap::ApplyEdit(const EditRecord &edit)
{
	const size_t first = edit.start.line;
	fRows.ReplaceLines(first, edit.oldEnd.line - first + 1, edit.newEnd.line - first + 1);
}


void DisplayMap::SetHiddenLines(const std::vector<std::pair<size_t, size_t>> &ranges)
{
	fRows.Reset(fRows.LineCount());
	for (const auto &[first, last] : ranges) {
		for (size_t line = first; line <= last && line < fRows.LineCount(); line++) {
			fRows.SetRows(line, 0);
		}
	}
}


DisplayRow DisplayMap::RowAt(size_t row) const
{
	const auto [line, subRow] = fRows.LineOfRow(row);
	return {line, subRow};
}


} // CodeEdit
