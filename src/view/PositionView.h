#pragma once

#include <View.h>


class CodeEditView;


namespace CodeEdit {


// Shows the caret position as <line>:<column> beside the horizontal scroll
// bar. It is only invalidated on changes; Draw() reads the current position.
class PositionView: public BView {
private:
	CodeEditView *fEditView;

public:
	explicit PositionView(CodeEditView *editView);

	float PreferredWidth();

	void Draw(BRect updateRect) final;
};


} // CodeEdit
