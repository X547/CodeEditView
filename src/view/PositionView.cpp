#include "PositionView.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include <CodeEditView/CodeEditView.h>


namespace CodeEdit {


static constexpr int kMinLineDigits = 4;
static constexpr float kPadding = 4;


PositionView::PositionView(CodeEditView *editView):
	BView("position", B_WILL_DRAW),
	fEditView(editView)
{
	SetViewColor(B_TRANSPARENT_COLOR);
	SetFont(be_plain_font);
}


// Wide enough for the line count and a three digit column.
float PositionView::PreferredWidth()
{
	size_t lineCount;
	{
		ModelLock lock(*fEditView->Model());
		lineCount = fEditView->Model()->Text().LineCount();
	}
	int digits = 1;
	for (size_t count = lineCount; count >= 10; count /= 10) {
		digits++;
	}
	const std::string sample = std::string(std::max(digits, kMinLineDigits), '0') + ":000";
	return ceilf(StringWidth(sample.c_str()) + 2 * kPadding);
}


void PositionView::Draw(BRect updateRect)
{
	(void)updateRect;
	const BRect bounds = Bounds();
	const rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	SetHighColor(panel);
	FillRect(bounds.InsetByCopy(1, 1));
	SetHighColor(tint_color(panel, B_DARKEN_2_TINT));
	StrokeRect(bounds);

	const TextPoint caret = fEditView->CaretLocation();
	char text[64];
	snprintf(text, sizeof(text), "%zu:%zu", caret.line + 1, caret.column + 1);

	font_height height;
	GetFontHeight(&height);
	const float baseline = floorf((bounds.top + bounds.bottom + height.ascent - height.descent) / 2 + 0.5f);
	SetHighUIColor(B_PANEL_TEXT_COLOR);
	SetLowColor(panel);
	DrawString(text, BPoint(bounds.left + kPadding, baseline));
}


} // CodeEdit
