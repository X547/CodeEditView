#include "OpenGitWindow.h"

#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <TextControl.h>

#include "Locator.h"
#include "WindowHelpers.h"


namespace Studio {


using Workspace::Locator;


static constexpr uint32 kMsgBrowse = 'OGbr';
static constexpr uint32 kMsgShow = 'OGsh';


OpenGitWindow::OpenGitWindow(
	const BMessenger &target,
	const std::string &directory,
	const std::string &revision,
	const std::string &path
):
	LocatorPanel("Open from Git", target),
	fRepository(new BTextControl("repository", "Repository:", directory.c_str(), new BMessage(kMsgShow))),
	fRevision(new BTextControl("revision", "Revision:", revision.c_str(), new BMessage(kMsgShow)))
{
	BLayoutBuilder::Group<>(HeaderLayout())
		.AddGrid()
			.AddTextControl(fRepository, 0, 0)
			.Add(new BButton("browse", "Browse" B_UTF8_ELLIPSIS, new BMessage(kMsgBrowse)), 2, 0)
			.AddTextControl(fRevision, 0, 1)
			.Add(new BButton("show", "Show", new BMessage(kMsgShow)), 2, 1)
		.End();

	if (directory.empty()) {
		SetStatus("Choose a repository");
	} else {
		ShowCommit(path);
	}
}


OpenGitWindow::~OpenGitWindow()
{
	delete fDirectoryPanel;
}


void OpenGitWindow::ShowCommit(const std::string &path)
{
	Navigate(Locator::GitCommit(fRepository->Text(), fRevision->Text(), path));
}


void OpenGitWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgBrowse:
			if (fDirectoryPanel == nullptr) {
				fDirectoryPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, B_DIRECTORY_NODE, false);
			}
			DiffKit::PlacePanel(fDirectoryPanel, Frame());
			fDirectoryPanel->Show();
			break;
		case B_REFS_RECEIVED: {
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				fRepository->SetText(BPath(&ref).Path());
				ShowCommit();
			}
			break;
		}
		case kMsgShow:
			ShowCommit();
			break;
		default:
			LocatorPanel::MessageReceived(message);
			break;
	}
}


} // Studio
