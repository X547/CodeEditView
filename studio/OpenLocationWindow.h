#pragma once

#include <Messenger.h>
#include <Window.h>


class BStringView;
class BTextControl;


namespace Studio {


// Asks for a locator string (or an absolute path) and sends kMsgOpenLocator
// to the target.
class OpenLocationWindow: public BWindow {
private:
	BMessenger fTarget;
	BTextControl *fLocation;
	BStringView *fStatus;

	void Open();

public:
	explicit OpenLocationWindow(const BMessenger &target);

	void MessageReceived(BMessage *message) override;
};


} // Studio
