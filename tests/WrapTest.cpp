#include <stdint.h>

#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/EditRecord.h>
#include <CodeEditView/TextModel.h>

#include "layout/DisplayMap.h"
#include "view/EditorCore.h"


namespace CodeEdit {


static BReference<TextModel> MakeModel(std::string_view text)
{
	BReference<TextModel> model(new TextModel(), true);
	ModelLock lock(*model);
	model->SetText(text);
	return model;
}


// Line 0 wraps into "aaaa bbbb " and "cccc" (indent 2) at 10 columns.
static constexpr const char *kSample = "aaaa bbbb cccc\nx";


static void EnableWrap(EditorCore &core, uint32_t columns)
{
	EditorSettings settings = core.Settings();
	settings.wordWrap = true;
	settings.wrapIndent = 2;
	core.SetSettings(settings);
	core.SetWrapColumns(columns);
	while (core.RewrapStep(100)) {
	}
}


static size_t MoveFrom(EditorCore &core, size_t offset, Movement movement)
{
	core.SetSelection({offset, offset});
	core.Move(movement, false, {});
	return core.GetSelection().caret;
}


static bool operator==(const CaretPosition &a, const CaretPosition &b)
{
	return a.row == b.row && a.column == b.column;
}


static bool operator==(const CellHit &a, const CellHit &b)
{
	return a.offset == b.offset && a.pastLineEnd == b.pastLineEnd;
}


TEST_CASE("display map keeps wrap rows and hidden lines apart")
{
	DisplayMap map;
	map.Reset(4);
	map.SetWrapRows(1, 3);
	CHECK(map.RowCount() == 6);
	map.SetHiddenLines({{1, 2}});
	CHECK(map.RowCount() == 2);
	map.SetHiddenLines({});
	CHECK(map.RowCount() == 6);
	CHECK(map.RowAt(3).line == 1);
	CHECK(map.RowAt(3).subRow == 2);

	map.MarkAllStale(2);
	CHECK(map.HasStaleLines());
	CHECK(map.NextStaleLine() == 2);
	map.SetWrapRows(2, 1);
	CHECK(map.NextStaleLine() == 3);
	map.SetWrapRows(3, 2);
	CHECK(map.NextStaleLine() == 0);
	map.SetWrapRows(0, 1);

	// Replacing line 1 removes its stale mark.
	EditRecord edit;
	edit.start = {1, 0};
	edit.oldEnd = {1, 0};
	edit.newEnd = {2, 0};
	map.ApplyEdit(edit);
	CHECK_FALSE(map.HasStaleLines());
	CHECK(map.LineCount() == 5);
	CHECK(map.RowCount() == 6);
}


TEST_CASE("positions in wrapped rows")
{
	EditorCore core(MakeModel(kSample));
	EnableWrap(core, 10);
	CHECK(core.RowCount() == 3);

	CHECK(core.PositionOfOffset(9) == CaretPosition {0, 9});
	CHECK(core.PositionOfOffset(10) == CaretPosition {1, 2});
	CHECK(core.PositionOfOffset(14) == CaretPosition {1, 6});
	CHECK(core.PositionOfOffset(15) == CaretPosition {2, 0});
	CHECK(core.CaretLocation() == TextPoint {0, 0});

	CHECK(core.OffsetAt(1, 0) == 10);
	CHECK(core.OffsetAt(1, 4) == 12);
	CHECK(core.OffsetAt(0, 20) == 9);
	CHECK(core.OffsetAt(1, 20) == 14);
	CHECK(core.RowStartOffset(1) == 10);

	CHECK(core.HitTestCell(0, 20) == CellHit {9, false});
	CHECK(core.HitTestCell(1, 0) == CellHit {10, false});
	CHECK(core.HitTestCell(1, 20) == CellHit {14, true});

	const std::vector<RowData> rows = core.Rows(0, 3);
	REQUIRE(rows.size() == 3);
	CHECK(rows[0].rowEnd == 10);
	CHECK_FALSE(rows[0].lastRow);
	CHECK(rows[1].subRow == 1);
	CHECK(rows[1].rowStart == 10);
	CHECK(rows[1].rowEnd == 14);
	CHECK(rows[1].rowColumn == 10);
	CHECK(rows[1].indent == 2);
	CHECK(rows[1].lastRow);
	CHECK(rows[1].DisplayColumn(12) == 4);
	CHECK(rows[2].line == 1);
}


TEST_CASE("movement by visual rows")
{
	EditorCore core(MakeModel(kSample));
	EnableWrap(core, 10);

	CHECK(MoveFrom(core, 3, Movement::NextLine) == 11);
	CHECK(MoveFrom(core, 12, Movement::PreviousLine) == 4);
	CHECK(MoveFrom(core, 12, Movement::NextLine) == 16);

	// Home and End go to the row edges first, then to the line edges.
	CHECK(MoveFrom(core, 2, Movement::LineEnd) == 9);
	CHECK(MoveFrom(core, 9, Movement::LineEnd) == 14);
	CHECK(MoveFrom(core, 12, Movement::LineStart) == 10);
	CHECK(MoveFrom(core, 10, Movement::LineStart) == 0);
	CHECK(MoveFrom(core, 12, Movement::LineEnd) == 14);
}


TEST_CASE("edits and width changes re-wrap lines")
{
	BReference<TextModel> model = MakeModel(kSample);
	EditorCore core(model);
	EnableWrap(core, 10);
	core.TakeDirty();

	core.SetSelection({14, 14});
	core.InsertText(" dddddd");
	CHECK(core.RowCount() == 4);
	CHECK(core.PositionOfOffset(15) == CaretPosition {2, 2});
	DirtyRows dirty = core.TakeDirty();
	CHECK(dirty.first == 0);
	CHECK(dirty.last == SIZE_MAX);
	CHECK(dirty.scrollRangeChanged);

	// Another view's edit is wrapped when synced.
	EditorCore other(model);
	other.SetSelection({0, 0});
	other.InsertText("zzzzzzzzzz\n");
	CHECK(core.RowCount() == 5);

	// After a width change, queries keep the old rows of stale lines.
	core.SetWrapColumns(40);
	core.PositionOfOffset(20);
	core.Rows(0, 5);
	CHECK(core.RowCount() == 5);
	// Wrapping the rows about to be shown.
	CHECK(core.WrapRows(1, 1));
	CHECK_FALSE(core.WrapRows(1, 1));
	CHECK(core.RowCount() == 3);
	CHECK(core.RewrapStep(1));
	CHECK_FALSE(core.RewrapStep(100));
	CHECK(core.RowCount() == 3);

	// Turning wrapping off keeps folds.
	core.Fold({0, 1});
	CHECK(core.RowCount() == 2);
	EditorSettings settings = core.Settings();
	settings.wordWrap = false;
	core.SetSettings(settings);
	CHECK(core.IsFolded(0));
	CHECK(core.RowCount() == 2);
	CHECK_FALSE(core.IsWrapping());
}


TEST_CASE("unwrapped lines need no wrap width")
{
	EditorCore core(MakeModel(kSample));
	EditorSettings settings = core.Settings();
	settings.wordWrap = true;
	core.SetSettings(settings);
	CHECK_FALSE(core.IsWrapping());
	CHECK(core.RowCount() == 2);
	core.SetWrapColumns(10);
	CHECK(core.IsWrapping());
	CHECK(core.RowCount() == 3);
}


} // CodeEdit
