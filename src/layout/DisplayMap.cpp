#include "DisplayMap.h"

#include <CodeEditView/EditRecord.h>


namespace CodeEdit {


void DisplayMap::Reset(size_t lineCount)
{
	fLines.assign(lineCount, LineState {});
	fRows.Reset(lineCount);
	fStaleCount = 0;
	fStaleCursor = 0;
}


void DisplayMap::UpdateRows(size_t line)
{
	const LineState &state = fLines[line];
	fRows.SetRows(line, state.hidden ? 0 : state.wrapRows);
}


void DisplayMap::ApplyEdit(const EditRecord &edit)
{
	const size_t first = edit.start.line;
	const size_t oldCount = edit.oldEnd.line - first + 1;
	const size_t newCount = edit.newEnd.line - first + 1;
	for (size_t line = first; line < first + oldCount; line++) {
		if (fLines[line].stale) {
			fStaleCount--;
		}
	}
	fLines.erase(fLines.begin() + first, fLines.begin() + first + oldCount);
	fLines.insert(fLines.begin() + first, newCount, LineState {});
	fRows.ReplaceLines(first, oldCount, newCount);
	if (fStaleCursor >= fLines.size()) {
		fStaleCursor = 0;
	}
}


void DisplayMap::SetHiddenLines(const std::vector<std::pair<size_t, size_t>> &ranges)
{
	for (size_t line = 0; line < fLines.size(); line++) {
		if (fLines[line].hidden) {
			fLines[line].hidden = false;
			UpdateRows(line);
		}
	}
	for (const auto &[first, last] : ranges) {
		for (size_t line = first; line <= last && line < fLines.size(); line++) {
			fLines[line].hidden = true;
			UpdateRows(line);
		}
	}
}


void DisplayMap::SetWrapRows(size_t line, uint32_t rows)
{
	LineState &state = fLines.at(line);
	if (state.stale) {
		state.stale = false;
		fStaleCount--;
	}
	if (rows != state.wrapRows) {
		state.wrapRows = rows;
		UpdateRows(line);
	}
}


void DisplayMap::MarkAllStale(size_t line)
{
	for (LineState &state : fLines) {
		state.stale = true;
	}
	fStaleCount = fLines.size();
	fStaleCursor = line < fLines.size() ? line : 0;
}


std::optional<size_t> DisplayMap::NextStaleLine()
{
	if (fStaleCount == 0) {
		return std::nullopt;
	}
	// Each call continues where the previous one stopped and wraps around.
	for (size_t i = 0; i < fLines.size(); i++) {
		const size_t line = fStaleCursor;
		fStaleCursor = fStaleCursor + 1 < fLines.size() ? fStaleCursor + 1 : 0;
		if (fLines[line].stale) {
			return line;
		}
	}
	return std::nullopt;
}


DisplayRow DisplayMap::RowAt(size_t row) const
{
	const auto [line, subRow] = fRows.LineOfRow(row);
	return {line, subRow};
}


} // CodeEdit
