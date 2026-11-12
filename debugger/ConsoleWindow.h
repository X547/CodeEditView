#pragma once

#include "Common.h"


class BTextControl;
class BTextView;


namespace Debugger {


// Output of the program and GDB, and a GDB command line.
class ConsoleWindow: public ToolWindow {
private:
	BTextView *fOutput;
	BTextControl *fInput;

	void Append(const char *text, const char *category);

protected:
	void SessionChanged() override;

public:
	explicit ConsoleWindow(const SessionContext &context);

	void MessageReceived(BMessage *message) override;
};


} // Debugger
