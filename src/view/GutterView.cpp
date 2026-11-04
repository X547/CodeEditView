#include "GutterView.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <vector>

#include <CodeEditView/CodeEditView.h>

#include "EditorCore.h"


namespace CodeEdit {


static constexpr int kMinDigits = 3;
// Cells: padding before the number, and the fold marker column after it.
static constexpr int kLeadingCells = 1;
static constexpr int kFoldCells = 2;


GutterView::GutterView(CodeEditView *editView):
	BView("gutter", B_WILL_DRAW),
	fEditView(editView)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	BFont font;
	editView->GetFont(&font);
	SetFont(&font);
}


float GutterView::PreferredWidth()
{
	int digits = 1;
	for (size_t count = fEditView->fCore->LineCount(); count >= 10; count /= 10) {
		digits++;
	}
	digits = std::max(digits, kMinDigits);
	return ceilf((kLeadingCells + digits + kFoldCells) * fEditView->fCellWidth);
}


void GutterView::Draw(BRect updateRect)
{
	const rgb_color background = ui_color(B_DOCUMENT_BACKGROUND_COLOR);
	const rgb_color text = ui_color(B_DOCUMENT_TEXT_COLOR);
	SetHighColor(mix_color(background, text, 16));
	FillRect(updateRect);

	EditorCore &core = *fEditView->fCore;
	const float lineHeight = fEditView->fLineHeight;
	const float cellWidth = fEditView->fCellWidth;
	const BRect bounds = Bounds();

	const size_t firstRow = fEditView->RowAtY(updateRect.top);
	const size_t lastRow = fEditView->RowAtY(updateRect.bottom);
	const std::vector<DisplayRow> rows = core.DisplayRows(firstRow, lastRow - firstRow + 1);
	const size_t caretRow = core.PositionOfOffset(core.GetSelection().caret).row;

	std::vector<FoldMarker> markers;
	if (!rows.empty()) {
		markers = core.FoldMarkers(rows.front().line, rows.back().line);
	}

	SetDrawingMode(B_OP_OVER);
	const rgb_color numberColor = mix_color(background, text, 110);
	const float foldLeft = bounds.right - kFoldCells * cellWidth;
	for (size_t i = 0; i < rows.size(); i++) {
		if (rows[i].subRow != 0) {
			continue;
		}
		const size_t row = firstRow + i;
		const float top = fEditView->RowTop(row);
		char number[32];
		snprintf(number, sizeof(number), "%zu", rows[i].line + 1);
		const float x = foldLeft - StringWidth(number);
		SetHighColor(row == caretRow ? text : numberColor);
		DrawString(number, BPoint(x, top + fEditView->fAscent));

		const auto marker = std::find_if(markers.begin(), markers.end(), [&](const FoldMarker &entry) {
			return entry.line == rows[i].line;
		});
		if (marker == markers.end()) {
			continue;
		}
		// A triangle pointing right when folded, down when expanded.
		const float size = std::max(3.0f, floorf(std::min(cellWidth, lineHeight / 2)));
		const float centerX = foldLeft + kFoldCells * cellWidth / 2;
		const float centerY = top + lineHeight / 2;
		SetHighColor(marker->folded ? text : numberColor);
		if (marker->folded) {
			FillTriangle(
				BPoint(centerX - size / 2, centerY - size / 2 - 1),
				BPoint(centerX - size / 2, centerY + size / 2 + 1),
				BPoint(centerX + size / 2, centerY)
			);
		} else {
			FillTriangle(
				BPoint(centerX - size / 2 - 1, centerY - size / 2),
				BPoint(centerX + size / 2 + 1, centerY - size / 2),
				BPoint(centerX, centerY + size / 2)
			);
		}
	}

	SetHighColor(mix_color(background, text, 48));
	StrokeLine(BPoint(bounds.right, updateRect.top), BPoint(bounds.right, updateRect.bottom));
}


void GutterView::MouseDown(BPoint where)
{
	EditorCore &core = *fEditView->fCore;
	const size_t row = fEditView->RowAtY(where.y);
	const std::vector<DisplayRow> rows = core.DisplayRows(row, 1);
	if (rows.empty() || rows.front().subRow != 0) {
		return;
	}
	core.ToggleFoldAtLine(rows.front().line);
	fEditView->ApplyDirty();
}


} // CodeEdit
