#pragma once

#include <StringView.h>


namespace DiffKit {


// A one-line label whose text does not set its minimum or preferred width:
// text that does not fit is shortened in the middle with an ellipsis when
// drawn, so the end of a path stays visible. The full text is the tool tip.
class TruncatingLabel: public BStringView {
public:
	TruncatingLabel(const char *name, const char *text);

	void SetLabel(const char *text);

	void Draw(BRect updateRect) override;
	BSize MinSize() override;
	BSize PreferredSize() override;
};


} // DiffKit
