#pragma once

#include <Window.h>


class CodeEditView;


class DemoWindow: public BWindow {
private:
	CodeEditView *fEditView {};

public:
	DemoWindow();

	bool QuitRequested() final;
};
