#pragma once

#include <View.h>


class CodeEditView;


namespace CodeEdit {


// Line marks, line numbers and fold markers beside a CodeEditView; scrolls
// vertically with it. Clicking a fold marker toggles the fold, clicking
// elsewhere sends kMsgGutterClicked to the view's target.
class GutterView: public BView {
private:
	CodeEditView *fEditView;

public:
	explicit GutterView(CodeEditView *editView);

	float PreferredWidth();

	void Draw(BRect updateRect) final;
	void MouseDown(BPoint where) final;
};


} // CodeEdit
