#include "OpenWindow.h"

#include <string.h>

#include <Application.h>
#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <TextControl.h>

#include "Messages.h"


namespace Coverage {


enum {
	kBinaryField,
	kProfileField,
};


static bool IsProfile(const char *path)
{
	const size_t length = strlen(path);
	return length >= 9 && strcmp(path + length - 9, ".profdata") == 0;
}


OpenWindow::OpenWindow(const std::string &binary, const std::string &profile):
	BWindow(
		BRect(200, 200, 759, 319),
		"Open coverage",
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE
	)
{
	const char *labels[] = {"Binary:", "Profile:"};
	const std::string paths[] = {binary, profile};
	BButton *browse[2];
	for (int field = 0; field < 2; field++) {
		fPaths[field] = new BTextControl(labels[field], paths[field].c_str(), nullptr);
		BMessage *message = new BMessage(kMsgBrowse);
		message->AddInt32("field", field);
		browse[field] = new BButton("Browse" B_UTF8_ELLIPSIS, message);
	}
	BButton *open = new BButton("Open", new BMessage(kMsgOpen));

	BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddTextControl(fPaths[kBinaryField], 0, 0)
		.Add(browse[kBinaryField], 2, 0)
		.AddTextControl(fPaths[kProfileField], 0, 1)
		.Add(browse[kProfileField], 2, 1)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0, 2, 3)
			.SetInsets(0, B_USE_SMALL_SPACING, 0, 0)
			.AddGlue()
			.Add(open)
		.End()
		.SetColumnWeight(1, 1);
	SetDefaultButton(open);
	fPaths[kBinaryField]->MakeFocus(true);
}


OpenWindow::~OpenWindow()
{
}


void OpenWindow::Browse(int field)
{
	if (fPanels[field] == nullptr) {
		BMessage message(B_REFS_RECEIVED);
		message.AddInt32("field", field);
		BMessenger target(this);
		fPanels[field] = std::make_unique<BFilePanel>(B_OPEN_PANEL, &target, nullptr, B_FILE_NODE, false, &message);
		fPanels[field]->Window()->SetTitle(field == kBinaryField ? "Instrumented binary" : "Merged profile (.profdata)");
	}
	fPanels[field]->Show();
}


void OpenWindow::Open()
{
	BMessage message(kMsgOpenCoverage);
	message.AddString("binary", fPaths[kBinaryField]->Text());
	message.AddString("profile", fPaths[kProfileField]->Text());
	be_app->PostMessage(&message);
	PostMessage(B_QUIT_REQUESTED);
}


bool OpenWindow::QuitRequested()
{
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void OpenWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgBrowse:
			Browse(message->GetInt32("field", kBinaryField));
			break;
		case kMsgOpen:
			Open();
			break;
		case B_REFS_RECEIVED:
		case B_SIMPLE_DATA: {
			const int32 field = message->GetInt32("field", -1);
			entry_ref ref;
			for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
				const BPath path(&ref);
				if (path.Path() == nullptr) {
					continue;
				}
				// Dropped files go where they belong by their extension.
				const int target = field >= 0 ? field : IsProfile(path.Path()) ? kProfileField : kBinaryField;
				fPaths[target]->SetText(path.Path());
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // Coverage
