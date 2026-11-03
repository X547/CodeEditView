#include "RowIndex.h"

#include <bit>
#include <stdexcept>


namespace CodeEdit {


void RowIndex::BuildTree() const
{
	const size_t count = fRows.size();
	fTree.assign(count + 1, 0);
	for (size_t i = 1; i <= count; i++) {
		fTree[i] += fRows[i - 1];
		const size_t parent = i + (i & (~i + 1));
		if (parent <= count) {
			fTree[parent] += fTree[i];
		}
	}
	fTreeValid = true;
}


void RowIndex::Reset(size_t lineCount, uint32_t rowsPerLine)
{
	fRows.assign(lineCount, rowsPerLine);
	fTotal = lineCount * rowsPerLine;
	fTreeValid = false;
}


void RowIndex::ReplaceLines(size_t first, size_t oldCount, size_t newCount, uint32_t rowsPerLine)
{
	if (first > fRows.size() || oldCount > fRows.size() - first) {
		throw std::out_of_range("RowIndex::ReplaceLines: range out of range");
	}
	for (size_t i = first; i < first + oldCount; i++) {
		fTotal -= fRows[i];
	}
	fRows.erase(fRows.begin() + first, fRows.begin() + first + oldCount);
	fRows.insert(fRows.begin() + first, newCount, rowsPerLine);
	fTotal += newCount * rowsPerLine;
	fTreeValid = false;
}


void RowIndex::SetRows(size_t line, uint32_t rows)
{
	const uint32_t oldRows = fRows.at(line);
	if (rows == oldRows) {
		return;
	}
	fRows[line] = rows;
	fTotal = fTotal - oldRows + rows;
	if (!fTreeValid) {
		return;
	}
	// Unsigned wrap-around makes the same update work for decreases.
	const size_t delta = static_cast<size_t>(rows) - oldRows;
	for (size_t i = line + 1; i < fTree.size(); i += i & (~i + 1)) {
		fTree[i] += delta;
	}
}


size_t RowIndex::FirstRow(size_t line) const
{
	if (line > fRows.size()) {
		throw std::out_of_range("RowIndex::FirstRow: line out of range");
	}
	if (!fTreeValid) {
		BuildTree();
	}
	size_t sum = 0;
	for (size_t i = line; i > 0; i -= i & (~i + 1)) {
		sum += fTree[i];
	}
	return sum;
}


std::pair<size_t, uint32_t> RowIndex::LineOfRow(size_t row) const
{
	if (row >= fTotal) {
		throw std::out_of_range("RowIndex::LineOfRow: row out of range");
	}
	if (!fTreeValid) {
		BuildTree();
	}
	// Largest prefix of lines whose total is <= row; the next line holds row.
	size_t pos = 0;
	size_t remaining = row;
	for (size_t step = std::bit_floor(fRows.size()); step > 0; step >>= 1) {
		if (pos + step < fTree.size() && fTree[pos + step] <= remaining) {
			pos += step;
			remaining -= fTree[pos];
		}
	}
	return {pos, static_cast<uint32_t>(remaining)};
}


} // CodeEdit
