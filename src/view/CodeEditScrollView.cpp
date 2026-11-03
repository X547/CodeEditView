#include <CodeEditView/CodeEditScrollView.h>

#include <algorithm>

#include <ControlLook.h>
#include <ScrollBar.h>

#include <CodeEditView/CodeEditView.h>

#include "GutterView.h"
#include "PositionView.h"


CodeEditScrollView::CodeEditScrollView(const char *name, CodeEditView *editView):
	BView(name, B_WILL_DRAW | B_FRAME_EVENTS),
	fEditView(editView),
	fGutter(new CodeEdit::GutterView(editView)),
	fPositionView(new CodeEdit::PositionView(editView)),
	fVerticalScrollBar(new BScrollBar("verticalScrollBar", editView, 0, 0, B_VERTICAL)),
	fHorizontalScrollBar(new BScrollBar("horizontalScrollBar", editView, 0, 0, B_HORIZONTAL))
{
	SetViewColor(B_TRANSPARENT_COLOR);
	fEditView->fScrollView = this;
	fEditView->fGutter = fGutter;
	AddChild(fGutter);
	AddChild(fEditView);
	AddChild(fVerticalScrollBar);
	AddChild(fPositionView);
	AddChild(fHorizontalScrollBar);
}


// Frame rectangles share border pixels: the scroll bars and the indicator lie
// on the 1 pixel frame and on each other's borders, the content fills the
// inside of the frame.
void CodeEditScrollView::LayoutChildren()
{
	const BRect bounds = Bounds();
	const float barWidth = be_control_look->GetScrollBarWidth(B_VERTICAL);
	const float barHeight = be_control_look->GetScrollBarWidth(B_HORIZONTAL);
	fPositionView->SetFontSize(std::min(be_plain_font->Size(), barHeight - 3));

	const float verticalBarLeft = bounds.right - barWidth;
	const float horizontalBarTop = bounds.bottom - barHeight;
	const float positionRight = bounds.left + fPositionView->PreferredWidth();
	const float gutterRight = bounds.left + fGutter->PreferredWidth();

	auto place = [](BView *view, float left, float top, float right, float bottom) {
		view->MoveTo(left, top);
		view->ResizeTo(right - left, bottom - top);
	};
	place(fGutter, bounds.left + 1, bounds.top + 1, gutterRight, horizontalBarTop - 1);
	place(fEditView, gutterRight + 1, bounds.top + 1, verticalBarLeft - 1, horizontalBarTop - 1);
	place(fVerticalScrollBar, verticalBarLeft, bounds.top, bounds.right, horizontalBarTop);
	place(fPositionView, bounds.left, horizontalBarTop, positionRight, bounds.bottom);
	place(fHorizontalScrollBar, positionRight, horizontalBarTop, verticalBarLeft, bounds.bottom);
}


void CodeEditScrollView::AttachedToWindow()
{
	BView::AttachedToWindow();
	LayoutChildren();
}


void CodeEditScrollView::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);
	LayoutChildren();
}


// Only the frame and the corner beside the scroll bars are visible.
void CodeEditScrollView::Draw(BRect updateRect)
{
	(void)updateRect;
	const BRect bounds = Bounds();
	const rgb_color panel = ui_color(B_PANEL_BACKGROUND_COLOR);
	const BRect corner(
		bounds.right - be_control_look->GetScrollBarWidth(B_VERTICAL), bounds.bottom - be_control_look->GetScrollBarWidth(B_HORIZONTAL),
		bounds.right, bounds.bottom
	);
	SetHighColor(panel);
	FillRect(corner);
	SetHighColor(tint_color(panel, B_DARKEN_2_TINT));
	StrokeRect(bounds);
}


BSize CodeEditScrollView::MinSize()
{
	return BSize(120, 80);
}


BSize CodeEditScrollView::PreferredSize()
{
	const BSize editSize = fEditView->PreferredSize();
	return BSize(
		fGutter->PreferredWidth() + editSize.width + be_control_look->GetScrollBarWidth(B_VERTICAL) + 2,
		editSize.height + be_control_look->GetScrollBarWidth(B_HORIZONTAL) + 2
	);
}


void CodeEditScrollView::ContentsChanged()
{
	const bool gutterChanged = fGutter->Bounds().Width() + 1 != fGutter->PreferredWidth();
	const bool positionChanged = fPositionView->Bounds().Width() != fPositionView->PreferredWidth();
	if (gutterChanged || positionChanged) {
		LayoutChildren();
	}
}


void CodeEditScrollView::CaretChanged()
{
	fPositionView->Invalidate();
}
