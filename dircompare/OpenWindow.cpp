#include "OpenWindow.h"

#include <vector>

#include <Application.h>
#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <TextControl.h>

#include "Messages.h"


namespace DirCompare {


OpenWindow::OpenWindow(const std::string &left, const std::string &right):
	BWindow(
		BRect(200, 200, 759, 319),
		"New comparison",
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE
	)
{
	const char *labels[] = {"Left:", "Right:"};
	const std::string paths[] = {left, right};
	BButton *browse[2];
	for (int pane = 0; pane < 2; pane++) {
		fPaths[pane] = new BTextControl(labels[pane], paths[pane].c_str(), nullptr);
		BMessage *message = new BMessage(kMsgBrowse);
		message->AddInt32("pane", pane);
		browse[pane] = new BButton("Browse" B_UTF8_ELLIPSIS, message);
	}
	BButton *compare = new BButton("Compare", new BMessage(kMsgCompare));

	BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddTextControl(fPaths[0], 0, 0)
		.Add(browse[0], 2, 0)
		.AddTextControl(fPaths[1], 0, 1)
		.Add(browse[1], 2, 1)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING, 0, 2, 3)
			.SetInsets(0, B_USE_SMALL_SPACING, 0, 0)
			.Add(new BButton("Swap", new BMessage(kMsgSwapPaths)))
			.AddGlue()
			.Add(compare)
		.End()
		.SetColumnWeight(1, 1);
	SetDefaultButton(compare);
	fPaths[0]->MakeFocus(true);
}


OpenWindow::~OpenWindow()
{
}


void OpenWindow::Browse(int pane)
{
	if (fPanels[pane] == nullptr) {
		BMessage message(B_REFS_RECEIVED);
		message.AddInt32("pane", pane);
		BMessenger target(this);
		fPanels[pane] = std::make_unique<BFilePanel>(B_OPEN_PANEL, &target, nullptr,
			B_FILE_NODE | B_DIRECTORY_NODE, false, &message);
		fPanels[pane]->Window()->SetTitle(pane == 0 ? "Left side" : "Right side");
	}
	fPanels[pane]->Show();
}


void OpenWindow::Compare()
{
	BMessage message(kMsgOpenComparison);
	message.AddString("left", fPaths[0]->Text());
	message.AddString("right", fPaths[1]->Text());
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
			Browse(message->GetInt32("pane", 0));
			break;
		case kMsgSwapPaths: {
			const BString left = fPaths[0]->Text();
			fPaths[0]->SetText(fPaths[1]->Text());
			fPaths[1]->SetText(left);
			break;
		}
		case kMsgCompare:
			Compare();
			break;
		case B_REFS_RECEIVED:
		case B_SIMPLE_DATA: {
			std::vector<BString> paths;
			entry_ref ref;
			for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
				paths.push_back(BPath(&ref).Path());
			}
			if (paths.empty()) {
				break;
			}
			const int32 pane = message->GetInt32("pane", -1);
			if (pane >= 0) {
				// From the panel of a side.
				fPaths[pane]->SetText(paths[0]);
			} else if (paths.size() >= 2) {
				fPaths[0]->SetText(paths[0]);
				fPaths[1]->SetText(paths[1]);
			} else {
				// One dropped entry fills the left side if it is empty.
				fPaths[fPaths[0]->Text()[0] == '\0' ? 0 : 1]->SetText(paths[0]);
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // DirCompare
