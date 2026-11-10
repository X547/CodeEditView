#include "DocumentsWindow.h"

#include <Application.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>

#include "Messages.h"
#include "StudioApp.h"


namespace Studio {


static constexpr uint32 kMsgInvoke = 'DWiv';


DocumentsWindow::DocumentsWindow():
	BWindow(BRect(80, 80, 479, 379), "CodeEdit", B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fList(new BListView("documents"))
{
	fList->SetInvocationMessage(new BMessage(kMsgInvoke));

	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("New", kMsgNewDocument)
			.AddItem("Open" B_UTF8_ELLIPSIS, kMsgShowOpenFile, 'O')
			.AddItem("Open from Git" B_UTF8_ELLIPSIS, kMsgShowOpenGit)
			.AddItem("Open location" B_UTF8_ELLIPSIS, kMsgShowOpenLocation, 'O', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
			.AddItem("Quit", B_QUIT_REQUESTED, 'Q')
		.End();
	// All but Close go to the application.
	for (const uint32 command : {kMsgNewDocument, kMsgShowOpenFile, kMsgShowOpenGit, kMsgShowOpenLocation}) {
		menuBar->FindItem(command)->SetTarget(be_app);
	}
	menuBar->FindItem("Quit")->SetTarget(be_app);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frame of the list overlaps the menu bar and window borders.
			.SetInsets(-1, -1, -1, -1)
			.Add(new BScrollView("scroll", fList, 0, false, true, B_PLAIN_BORDER))
		.End();

	StudioApp::Get().Registry().AddListener(BMessenger(this));
	Refresh();
}


DocumentsWindow::~DocumentsWindow()
{
	StudioApp::Get().Registry().RemoveListener(BMessenger(this));
}


void DocumentsWindow::Refresh()
{
	std::vector<Workspace::Locator> locators;
	for (const BReference<Workspace::Document> &document : StudioApp::Get().Registry().All()) {
		locators.push_back(document->GetLocator());
	}
	if (locators == fLocators) {
		return;
	}
	fLocators = std::move(locators);
	const int32 selection = fList->CurrentSelection();
	for (int32 i = fList->CountItems() - 1; i >= 0; i--) {
		delete fList->RemoveItem(i);
	}
	for (const Workspace::Locator &locator : fLocators) {
		fList->AddItem(new BStringItem(locator.DisplayName().c_str()));
	}
	fList->Select(selection);
}


void DocumentsWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case Workspace::kNoticeDocumentChanged:
		case kMsgWindowClosed:
			Refresh();
			break;
		case kMsgInvoke: {
			const int32 index = fList->CurrentSelection();
			if (index >= 0 && index < static_cast<int32>(fLocators.size())) {
				BMessage open(kMsgOpenLocator);
				open.AddString("locator", fLocators[index].ToString().c_str());
				be_app->PostMessage(&open);
			}
			break;
		}
		case kMsgActivate:
			Refresh();
			Activate();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool DocumentsWindow::QuitRequested()
{
	StudioApp::Get().DocumentsWindowClosing();
	return true;
}


} // Studio
