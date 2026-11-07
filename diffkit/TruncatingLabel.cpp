#include "TruncatingLabel.h"

#include <math.h>

#include <LayoutUtils.h>
#include <String.h>


namespace DiffKit {


TruncatingLabel::TruncatingLabel(const char *name, const char *text):
	BStringView(name, text, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
}


void TruncatingLabel::SetLabel(const char *text)
{
	SetText(text);
	SetToolTip(text != nullptr && text[0] != '\0' ? text : nullptr);
}


void TruncatingLabel::Draw(BRect updateRect)
{
	(void)updateRect;
	const BRect bounds = Bounds();
	SetLowColor(ViewColor());
	FillRect(bounds, B_SOLID_LOW);
	if (Text() == nullptr) {
		return;
	}
	BFont font;
	GetFont(&font);
	BString text(Text());
	font.TruncateString(&text, B_TRUNCATE_MIDDLE, bounds.Width());
	font_height height;
	font.GetHeight(&height);
	const float baseline = floorf(bounds.top + (bounds.Height() + 1 - height.ascent - height.descent) / 2
		+ height.ascent);
	DrawString(text.String(), BPoint(bounds.left, baseline));
}


BSize TruncatingLabel::MinSize()
{
	BSize size = BStringView::MinSize();
	size.width = StringWidth(B_UTF8_ELLIPSIS) * 3;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


BSize TruncatingLabel::PreferredSize()
{
	BSize size = BStringView::PreferredSize();
	size.width = StringWidth(B_UTF8_ELLIPSIS) * 3;
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


} // DiffKit
