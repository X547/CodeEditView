#pragma once

#include <View.h>


class BMessageRunner;
class CodeEditView;


namespace CodeEdit {


// Shows the caret position as <line>:<column> beside the horizontal scroll
// bar. It is only invalidated on changes; Draw() reads the current position.
// Clicks post kMsgPositionClicked or kMsgPositionHeld to the window.
class PositionView: public BView {
private:
	CodeEditView *fEditView;
	// Between a primary button press and the message it gives.
	bool fPressed {};
	BPoint fPressPoint;
	BMessageRunner *fHoldRunner {};

	void EndPress();
	void PostToWindow(uint32 what);

public:
	explicit PositionView(CodeEditView *editView);
	~PositionView() override;

	float PreferredWidth();

	void Draw(BRect updateRect) final;
	void MouseDown(BPoint where) final;
	void MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage) final;
	void MouseUp(BPoint where) final;
	void MessageReceived(BMessage *message) final;
};


} // CodeEdit
