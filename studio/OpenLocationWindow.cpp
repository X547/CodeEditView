#include "OpenLocationWindow.h"

#include <string>

#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>

#include "Locator.h"
#include "Messages.h"


namespace Studio {


static constexpr uint32 kMsgOpen = 'OLop';


OpenLocationWindow::OpenLocationWindow(const BMessenger &target):
	BWindow(BRect(0, 0, 559, 99), "Open location", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fTarget(target),
	fLocation(new BTextControl("location", "Location:", "", nullptr)),
	fStatus(new BStringView("status", "A path, file://, git:// or sftp:// location"))
{
	BButton *openButton = new BButton("open", "Open", new BMessage(kMsgOpen));
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fLocation)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(openButton)
		.End();
	SetDefaultButton(openButton);
	fLocation->MakeFocus(true);
	CenterOnScreen();
}


void OpenLocationWindow::Open()
{
	const std::string text = fLocation->Text();
	std::optional<Workspace::Locator> locator;
	if (text.starts_with('/')) {
		locator = Workspace::Locator::File(text);
	} else {
		locator = Workspace::Locator::Parse(text);
	}
	if (!locator.has_value()) {
		fStatus->SetText("Not a location: use an absolute path or a file://, git:// or sftp:// location");
		return;
	}
	BMessage message(kMsgOpenLocator);
	message.AddString("locator", locator->ToString().c_str());
	fTarget.SendMessage(&message);
	PostMessage(B_QUIT_REQUESTED);
}


void OpenLocationWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgOpen:
			Open();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // Studio
