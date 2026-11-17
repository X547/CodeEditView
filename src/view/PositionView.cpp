#include "PositionView.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include <MessageRunner.h>
#include <Window.h>

#include <CodeEditView/CodeEditView.h>


namespace CodeEdit {


static constexpr int kMinLineDigits = 4;
static constexpr float kPadding = 4;
// A press longer than this, or moving further, is a hold (as in Pe).
static constexpr bigtime_t kHoldDelay = 250000;
static constexpr float kDragDistance = 2;
static constexpr uint32 kMsgHoldTimeout = 'CEht';


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


PositionView::~PositionView()
{
	delete fHoldRunner;
}


void PositionView::PostToWindow(uint32 what)
{
	BMessage message(what);
	message.AddRect("frame", ConvertToScreen(Bounds()));
	Window()->PostMessage(&message);
}


void PositionView::EndPress()
{
	fPressed = false;
	delete fHoldRunner;
	fHoldRunner = nullptr;
}


// The hold is timed by a message, so the window keeps running meanwhile.
void PositionView::MouseDown(BPoint where)
{
	if (Window() == nullptr) {
		return;
	}
	fEditView->MakeFocus(true);
	EndPress();
	int32 buttons = 0;
	const BMessage *current = Window()->CurrentMessage();
	if (current != nullptr) {
		buttons = current->GetInt32("buttons", 0);
	}
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		PostToWindow(kMsgPositionHeld);
		return;
	}
	SetMouseEventMask(B_POINTER_EVENTS);
	fPressed = true;
	fPressPoint = where;
	BMessage hold(kMsgHoldTimeout);
	fHoldRunner = new BMessageRunner(BMessenger(this), &hold, kHoldDelay, 1);
}


void PositionView::MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage)
{
	(void)transit;
	(void)dragMessage;
	if (!fPressed) {
		return;
	}
	const bool dragged = fabsf(where.x - fPressPoint.x) > kDragDistance
		|| fabsf(where.y - fPressPoint.y) > kDragDistance;
	if (dragged) {
		EndPress();
		PostToWindow(kMsgPositionHeld);
	}
}


void PositionView::MouseUp(BPoint where)
{
	(void)where;
	if (fPressed) {
		EndPress();
		PostToWindow(kMsgPositionClicked);
	}
}


void PositionView::MessageReceived(BMessage *message)
{
	if (message->what != kMsgHoldTimeout) {
		BView::MessageReceived(message);
		return;
	}
	// A timeout sent before the press ended is ignored.
	if (fPressed) {
		EndPress();
		PostToWindow(kMsgPositionHeld);
	}
}


} // CodeEdit
