#pragma once

#include <View.h>


class CodeEditView;


namespace CodeEdit {


// Line numbers and fold markers beside a CodeEditView; scrolls vertically
// with it. Clicking a marker toggles the fold.
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
