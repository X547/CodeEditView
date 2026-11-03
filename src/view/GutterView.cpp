#include "GutterView.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <vector>

#include <CodeEditView/CodeEditView.h>

#include "EditorCore.h"


namespace CodeEdit {


static constexpr int kMinDigits = 3;


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
	return ceilf((digits + 2) * fEditView->fCellWidth);
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

	const size_t firstRow = static_cast<size_t>(std::max(0.0f, floorf(updateRect.top / lineHeight)));
	const size_t lastRow = static_cast<size_t>(std::max(0.0f, floorf(updateRect.bottom / lineHeight)));
	const std::vector<DisplayRow> rows = core.DisplayRows(firstRow, lastRow - firstRow + 1);
	const size_t caretRow = core.PositionOfOffset(core.GetSelection().caret).row;

	SetDrawingMode(B_OP_OVER);
	const rgb_color numberColor = mix_color(background, text, 110);
	for (size_t i = 0; i < rows.size(); i++) {
		if (rows[i].subRow != 0) {
			continue;
		}
		const size_t row = firstRow + i;
		char number[32];
		snprintf(number, sizeof(number), "%zu", rows[i].line + 1);
		const float x = bounds.right - cellWidth - StringWidth(number);
		SetHighColor(row == caretRow ? text : numberColor);
		DrawString(number, BPoint(x, row * lineHeight + fEditView->fAscent));
	}

	SetHighColor(mix_color(background, text, 48));
	StrokeLine(BPoint(bounds.right, updateRect.top), BPoint(bounds.right, updateRect.bottom));
}


} // CodeEdit
