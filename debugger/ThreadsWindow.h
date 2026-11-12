#pragma once

#include <stdint.h>

#include "Common.h"


class BColumnListView;


namespace Debugger {


// The threads of the program; selecting one shows its stack.
class ThreadsWindow: public ToolWindow {
private:
	BColumnListView *fList;
	int64_t fCurrent {};

	void Update(const BMessage &notice);

protected:
	void SessionChanged() override;

public:
	explicit ThreadsWindow(const SessionContext &context);

	void MessageReceived(BMessage *message) override;
};


} // Debugger
