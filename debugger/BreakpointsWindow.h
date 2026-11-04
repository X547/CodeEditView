#pragma once

#include "Common.h"


class BButton;
class BColumnListView;


namespace Debugger {


// All breakpoints with their state. They are breakpoint marks in the
// source documents, so changes here show in the gutters and vice versa.
class BreakpointsWindow: public ToolWindow {
private:
	BColumnListView *fList;
	BButton *fEnableButton;
	BButton *fRemoveButton;

	void Update(const BMessage &notice);
	void UpdateButtons();

public:
	BreakpointsWindow();

	void MessageReceived(BMessage *message) override;
};


} // Debugger
