#pragma once

#include <View.h>


class CodeEditView: public BView {
public:
	explicit CodeEditView(const char *name);
	~CodeEditView() override;

	void Draw(BRect updateRect) override;
};
