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


DisplayRow DisplayMap::RowAt(size_t row) const
{
	const auto [line, subRow] = fRows.LineOfRow(row);
	return {line, subRow};
}


} // CodeEdit
