#pragma once

#include <View.h>


class BScrollBar;
class CodeEditView;


namespace CodeEdit {


class GutterView;
class PositionView;


} // CodeEdit


// Puts a CodeEditView together with a line number gutter, scroll bars and a
// caret position indicator inside a 1 pixel frame. The borders of the scroll
// bars and the indicator overlap the frame instead of doubling it.
class CodeEditScrollView: public BView {
private:
	CodeEditView *fEditView;
	CodeEdit::GutterView *fGutter;
	CodeEdit::PositionView *fPositionView;
	BScrollBar *fVerticalScrollBar;
	BScrollBar *fHorizontalScrollBar;

	friend class CodeEditView;

	void LayoutChildren();

public:
	CodeEditScrollView(const char *name, CodeEditView *editView);

	CodeEditView *EditView() const {return fEditView;}

	void AttachedToWindow() override;
	void FrameResized(float width, float height) override;
	void Draw(BRect updateRect) override;
	BSize MinSize() override;
	BSize PreferredSize() override;

	// Called when the gutter or indicator width may have changed.
	void ContentsChanged();
	// Called when the caret moved or the text changed.
	void CaretChanged();
};
