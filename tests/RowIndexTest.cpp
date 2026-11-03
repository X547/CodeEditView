#include <algorithm>
#include <iterator>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "layout/DisplayMap.h"
#include "layout/RowIndex.h"


namespace CodeEdit {


static void CheckAgainst(const RowIndex &index, const std::vector<uint32_t> &rows)
{
	REQUIRE(index.LineCount() == rows.size());
	const size_t total = std::accumulate(rows.begin(), rows.end(), size_t {0});
	REQUIRE(index.TotalRows() == total);

	size_t row = 0;
	for (size_t line = 0; line < rows.size(); line++) {
		REQUIRE(index.Rows(line) == rows[line]);
		REQUIRE(index.FirstRow(line) == row);
		for (uint32_t subRow = 0; subRow < rows[line]; subRow++) {
			const auto [foundLine, foundSubRow] = index.LineOfRow(row + subRow);
			REQUIRE(foundLine == line);
			REQUIRE(foundSubRow == subRow);
		}
		row += rows[line];
	}
	REQUIRE(index.FirstRow(rows.size()) == total);
	CHECK_THROWS_AS(index.LineOfRow(total), std::out_of_range);
}


TEST_CASE("row index basics")
{
	RowIndex index;
	index.Reset(5);
	CheckAgainst(index, {1, 1, 1, 1, 1});

	index.SetRows(1, 3);
	index.SetRows(2, 0);
	CheckAgainst(index, {1, 3, 0, 1, 1});

	index.ReplaceLines(1, 2, 3);
	CheckAgainst(index, {1, 1, 1, 1, 1, 1});

	index.ReplaceLines(0, 6, 0);
	CheckAgainst(index, {});
	CHECK_THROWS_AS(index.ReplaceLines(1, 0, 1), std::out_of_range);
}


TEST_CASE("row index randomized against a vector")
{
	std::mt19937 rng(4);
	RowIndex index;
	std::vector<uint32_t> rows(300, 1);
	index.Reset(rows.size());

	for (int step = 0; step < 2000; step++) {
		if (std::uniform_int_distribution<int>(0, 2)(rng) == 0) {
			const size_t first = std::uniform_int_distribution<size_t>(0, rows.size())(rng);
			const size_t oldCount = std::uniform_int_distribution<size_t>(0, std::min<size_t>(5, rows.size() - first))(rng);
			const size_t newCount = std::uniform_int_distribution<size_t>(0, 5)(rng);
			index.ReplaceLines(first, oldCount, newCount);
			rows.erase(rows.begin() + first, rows.begin() + first + oldCount);
			rows.insert(rows.begin() + first, newCount, 1);
		} else if (!rows.empty()) {
			const size_t line = std::uniform_int_distribution<size_t>(0, rows.size() - 1)(rng);
			const uint32_t count = std::uniform_int_distribution<uint32_t>(0, 4)(rng);
			index.SetRows(line, count);
			rows[line] = count;
		}
		if (step % 50 == 0) {
			CheckAgainst(index, rows);
		}
	}
	CheckAgainst(index, rows);
}


TEST_CASE("display map follows model edits")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("one\ntwo\nthree\n");
	const ObserverId observer = model.AddObserver();

	DisplayMap map;
	map.Reset(model.Text().LineCount());

	std::mt19937 rng(5);
	const char *const kInserts[] = {"x", "\n", "a\nb", "\n\n\n", ""};
	for (int step = 0; step < 500; step++) {
		const size_t length = model.Text().Length();
		const size_t offset = std::uniform_int_distribution<size_t>(0, length)(rng);
		const size_t erase = std::uniform_int_distribution<size_t>(0, std::min<size_t>(6, length - offset))(rng);
		const char *text = kInserts[std::uniform_int_distribution<size_t>(0, std::size(kInserts) - 1)(rng)];
		model.Replace(offset, erase, text);

		const ModelChanges changes = model.Sync(observer);
		REQUIRE_FALSE(changes.reset);
		for (const EditRecord &edit : changes.edits) {
			map.ApplyEdit(edit);
		}
		REQUIRE(map.LineCount() == model.Text().LineCount());
		REQUIRE(map.RowCount() == model.Text().LineCount());
	}

	const DisplayRow row = map.RowAt(map.RowCount() - 1);
	CHECK(row.line == model.Text().LineCount() - 1);
	CHECK(row.subRow == 0);
	model.RemoveObserver(observer);
}


} // CodeEdit
