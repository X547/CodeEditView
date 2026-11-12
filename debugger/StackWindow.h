#pragma once

#include "Common.h"


class BColumnListView;


namespace Debugger {


// The stack of the current thread. Selecting a frame shows its variables;
// double click shows its source.
class StackWindow: public ToolWindow {
private:
	BColumnListView *fList;
	int32 fCurrent {};

	void Update(const BMessage &notice);
	void SelectFrame(bool showSource);

protected:
	void SessionChanged() override;

public:
	explicit StackWindow(const SessionContext &context);

	void MessageReceived(BMessage *message) override;
};


} // Debugger
