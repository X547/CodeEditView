#pragma once

#include <View.h>


class CodeEditView;


namespace CodeEdit {


// Line numbers beside a CodeEditView; scrolls vertically with it.
class GutterView: public BView {
private:
	CodeEditView *fEditView;

public:
	explicit GutterView(CodeEditView *editView);

	float PreferredWidth();

	void Draw(BRect updateRect) final;
};


} // CodeEdit
