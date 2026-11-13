#include "GutterView.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <vector>

#include <Window.h>

#include <CodeEditView/CodeEditView.h>

#include "EditorCore.h"


namespace CodeEdit {


static constexpr int kMinDigits = 3;
// Cells of the fold marker column after the number.
static constexpr int kFoldCells = 2;


// The mark column comes first, about square.
static float MarkColumnWidth(float lineHeight, float cellWidth)
{
	return ceilf(std::max(lineHeight, 2 * cellWidth));
}


static void DrawMark(BView *view, const MarkStyle &style, BRect frame)
{
	const float size = std::min(frame.Width(), frame.Height());
	const BPoint center((frame.left + frame.right) / 2, (frame.top + frame.bottom) / 2);
	const float half = floorf(size / 2) - 2;
	if (half < 2) {
		return;
	}
	const rgb_color outline = tint_color(style.color, B_DARKEN_2_TINT);
	switch (style.shape) {
		case MarkShape::Circle:
			view->SetHighColor(style.color);
			view->FillEllipse(center, half, half);
			view->SetHighColor(outline);
			view->StrokeEllipse(center, half, half);
			break;
		case MarkShape::CircleOutline:
			view->SetHighColor(style.color);
			view->SetPenSize(2);
			view->StrokeEllipse(center, half - 0.5f, half - 0.5f);
			view->SetPenSize(1);
			break;
		case MarkShape::Bookmark: {
			// A ribbon with a notch at the bottom.
			const float left = center.x - roundf(half * 0.7f);
			const float right = center.x + roundf(half * 0.7f);
			const float top = center.y - half;
			const float bottom = center.y + half;
			const BPoint points[] = {
				BPoint(left, top), BPoint(right, top), BPoint(right, bottom),
				BPoint(center.x, bottom - roundf(half * 0.6f)), BPoint(left, bottom)
			};
			view->SetHighColor(style.color);
			view->FillPolygon(points, 5);
			view->SetHighColor(outline);
			view->StrokePolygon(points, 5);
			break;
		}
		case MarkShape::Arrow: {
			const float left = center.x - half;
			const float right = center.x + half;
			const float shaft = roundf(half * 0.4f);
			const BPoint points[] = {
				BPoint(left, center.y - shaft), BPoint(center.x, center.y - shaft),
				BPoint(center.x, center.y - half), BPoint(right, center.y),
				BPoint(center.x, center.y + half), BPoint(center.x, center.y + shaft),
				BPoint(left, center.y + shaft)
			};
			view->SetHighColor(style.color);
			view->FillPolygon(points, 7);
			view->SetHighColor(outline);
			view->StrokePolygon(points, 7);
			break;
		}
		default:
			break;
	}
}


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
	const float cellWidth = fEditView->fCellWidth;
	return MarkColumnWidth(fEditView->fLineHeight, cellWidth) + ceilf((digits + kFoldCells) * cellWidth);
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
	std::vector<MarkInfo> marks;
	if (!rows.empty()) {
		markers = core.FoldMarkers(rows.front().line, rows.back().line);
		marks = core.Marks(rows.front().line, rows.back().line);
	}
	const float markWidth = MarkColumnWidth(lineHeight, cellWidth);

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

		// Marks of the line, higher priorities on top.
		std::vector<MarkStyle> styles;
		for (const MarkInfo &mark : marks) {
			if (mark.line == rows[i].line) {
				styles.push_back(fEditView->GetMarkStyle(mark.kind));
			}
		}
		std::stable_sort(styles.begin(), styles.end(), [](const MarkStyle &a, const MarkStyle &b) {
			return a.priority < b.priority;
		});
		for (const MarkStyle &style : styles) {
			DrawMark(this, style, BRect(0, top, markWidth - 1, top + lineHeight - 1));
		}

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

	// The numbers of the sticky lines, inner ones first as in the view.
	const std::vector<StickyRow> &sticky = fEditView->fStickyRows;
	if (!sticky.empty() && updateRect.top < fEditView->fStickyHeight) {
		for (auto it = sticky.rbegin(); it != sticky.rend(); ++it) {
			SetHighColor(mix_color(background, text, 28));
			FillRect(BRect(bounds.left, it->top, bounds.right, it->top + lineHeight - 1));
			char number[32];
			snprintf(number, sizeof(number), "%zu", it->line + 1);
			SetHighColor(numberColor);
			DrawString(number, BPoint(foldLeft - StringWidth(number), it->top + fEditView->fAscent));
		}
		const float bottom = fEditView->fStickyHeight - 1;
		SetHighColor(mix_color(background, text, 64));
		StrokeLine(BPoint(bounds.left, bottom), BPoint(bounds.right, bottom));
	}

	SetHighColor(mix_color(background, text, 48));
	StrokeLine(BPoint(bounds.right, updateRect.top), BPoint(bounds.right, updateRect.bottom));
}


void GutterView::MouseDown(BPoint where)
{
	if (const std::optional<size_t> sticky = fEditView->StickyRowAt(where.y)) {
		fEditView->GoToStickyLine(*sticky, fEditView->Bounds().left);
		return;
	}
	EditorCore &core = *fEditView->fCore;
	const size_t row = fEditView->RowAtY(where.y);
	const std::vector<DisplayRow> rows = core.DisplayRows(row, 1);
	if (rows.empty()) {
		return;
	}
	const float foldLeft = Bounds().right - kFoldCells * fEditView->fCellWidth;
	if (where.x >= foldLeft) {
		if (rows.front().subRow == 0) {
			core.ToggleFoldAtLine(rows.front().line);
			fEditView->ApplyDirty();
		}
		return;
	}

	if (!fEditView->fTarget.IsValid()) {
		return;
	}
	const BMessage *current = Window()->CurrentMessage();
	BMessage message(kMsgGutterClicked);
	message.AddPointer("view", fEditView);
	message.AddUInt64("line", rows.front().line);
	message.AddInt32("buttons", current != nullptr ? current->GetInt32("buttons", B_PRIMARY_MOUSE_BUTTON) : B_PRIMARY_MOUSE_BUTTON);
	message.AddInt32("modifiers", current != nullptr ? current->GetInt32("modifiers", 0) : static_cast<int32>(modifiers()));
	message.AddInt32("clicks", current != nullptr ? current->GetInt32("clicks", 1) : 1);
	fEditView->fTarget.SendMessage(&message);
}


} // CodeEdit
