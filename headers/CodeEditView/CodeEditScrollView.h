#pragma once

#include <View.h>


class BScrollBar;
class CodeEditView;


namespace CodeEdit {


class GutterView;


} // CodeEdit


// Puts a CodeEditView together with a line number gutter and scroll bars.
class CodeEditScrollView: public BView {
private:
	CodeEditView *fEditView;
	CodeEdit::GutterView *fGutter;
	BScrollBar *fVerticalScrollBar;
	BScrollBar *fHorizontalScrollBar;

	friend class CodeEditView;

	void LayoutChildren();

public:
	CodeEditScrollView(const char *name, CodeEditView *editView);

	CodeEditView *EditView() const {return fEditView;}

	void AttachedToWindow() override;
	void FrameResized(float width, float height) override;
	BSize MinSize() override;
	BSize PreferredSize() override;

	// Called when the gutter width may have changed.
	void ContentsChanged();
};
