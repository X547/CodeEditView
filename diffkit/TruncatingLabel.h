#pragma once

#include <Invoker.h>
#include <StringView.h>


namespace DiffKit {


// A one-line label whose text does not set its minimum or preferred width:
// text that does not fit is shortened in the middle with an ellipsis when
// drawn, so the end of a path stays visible. The full text is the tool tip.
// As a link, it is drawn as one and a click sends its message (BInvoker).
class TruncatingLabel: public BStringView, public BInvoker {
private:
	bool fLink {};

public:
	TruncatingLabel(const char *name, const char *text);

	void SetLabel(const char *text);
	void SetLink(bool link);
	bool IsLink() const {return fLink;}

	void Draw(BRect updateRect) override;
	void MouseDown(BPoint where) override;
	void MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage) override;
	BSize MinSize() override;
	BSize PreferredSize() override;
};


} // DiffKit
