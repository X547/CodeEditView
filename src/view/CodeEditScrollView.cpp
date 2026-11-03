#include <CodeEditView/CodeEditScrollView.h>

#include <ControlLook.h>
#include <ScrollBar.h>

#include <CodeEditView/CodeEditView.h>

#include "GutterView.h"


CodeEditScrollView::CodeEditScrollView(const char *name, CodeEditView *editView):
	BView(name, B_FRAME_EVENTS),
	fEditView(editView),
	fGutter(new CodeEdit::GutterView(editView)),
	fVerticalScrollBar(new BScrollBar("verticalScrollBar", editView, 0, 0, B_VERTICAL)),
	fHorizontalScrollBar(new BScrollBar("horizontalScrollBar", editView, 0, 0, B_HORIZONTAL))
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	fEditView->fScrollView = this;
	fEditView->fGutter = fGutter;
	AddChild(fGutter);
	AddChild(fEditView);
	AddChild(fVerticalScrollBar);
	AddChild(fHorizontalScrollBar);
}


void CodeEditScrollView::LayoutChildren()
{
	const BRect bounds = Bounds();
	const float barWidth = be_control_look->GetScrollBarWidth(B_VERTICAL);
	const float barHeight = be_control_look->GetScrollBarWidth(B_HORIZONTAL);
	const float gutterWidth = fGutter->PreferredWidth();

	const float verticalBarLeft = bounds.right - barWidth;
	const float horizontalBarTop = bounds.bottom - barHeight;

	fGutter->MoveTo(0, 0);
	fGutter->ResizeTo(gutterWidth - 1, horizontalBarTop - 1);
	fEditView->MoveTo(gutterWidth, 0);
	fEditView->ResizeTo(verticalBarLeft - 1 - gutterWidth, horizontalBarTop - 1);
	fVerticalScrollBar->MoveTo(verticalBarLeft, 0);
	fVerticalScrollBar->ResizeTo(barWidth, horizontalBarTop - 1);
	fHorizontalScrollBar->MoveTo(0, horizontalBarTop);
	fHorizontalScrollBar->ResizeTo(verticalBarLeft - 1, barHeight);
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


BSize CodeEditScrollView::MinSize()
{
	return BSize(120, 80);
}


BSize CodeEditScrollView::PreferredSize()
{
	const BSize editSize = fEditView->PreferredSize();
	return BSize(
		fGutter->PreferredWidth() + editSize.width + be_control_look->GetScrollBarWidth(B_VERTICAL),
		editSize.height + be_control_look->GetScrollBarWidth(B_HORIZONTAL)
	);
}


void CodeEditScrollView::ContentsChanged()
{
	if (fGutter->Bounds().Width() + 1 != fGutter->PreferredWidth()) {
		LayoutChildren();
	}
}
