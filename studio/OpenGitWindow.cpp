#include "OpenGitWindow.h"

#include <algorithm>

#include <Button.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>

#include "Messages.h"
#include "WindowHelpers.h"


namespace Studio {


using Workspace::DirEntry;
using Workspace::Locator;
using Workspace::Provider;
using Workspace::ProviderError;
using Workspace::Providers;


static constexpr uint32 kMsgBrowse = 'OGbr';
static constexpr uint32 kMsgShow = 'OGsh';
static constexpr uint32 kMsgOpen = 'OGop';


OpenGitWindow::OpenGitWindow(const BMessenger &target, const std::string &directory):
	BWindow(BRect(0, 0, 559, 499), "Open from Git", B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fRepository(new BTextControl("repository", "Repository:", directory.c_str(), nullptr)),
	fRevision(new BTextControl("revision", "Revision:", "HEAD", new BMessage(kMsgShow))),
	fPlace(new BStringView("place", "")),
	fList(new BListView("entries")),
	fStatus(new BStringView("status", ""))
{
	fList->SetInvocationMessage(new BMessage(kMsgOpen));
	fPlace->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fPlace->SetTruncation(B_TRUNCATE_BEGINNING);
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStatus->SetTruncation(B_TRUNCATE_END);

	BButton *openButton = new BButton("open", "Open", new BMessage(kMsgOpen));
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid()
			.AddTextControl(fRepository, 0, 0)
			.Add(new BButton("browse", "Browse" B_UTF8_ELLIPSIS, new BMessage(kMsgBrowse)), 2, 0)
			.AddTextControl(fRevision, 0, 1)
			.Add(new BButton("show", "Show", new BMessage(kMsgShow)), 2, 1)
		.End()
		.Add(fPlace)
		.Add(new BScrollView("scroll", fList, 0, false, true), 1)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(openButton)
		.End();
	SetDefaultButton(openButton);
	CenterOnScreen();

	if (!directory.empty()) {
		ShowCommit();
	}
	fList->MakeFocus(true);
}


OpenGitWindow::~OpenGitWindow()
{
	delete fDirectoryPanel;
}


void OpenGitWindow::SetStatus(const std::string &text)
{
	fStatus->SetText(text.c_str());
}


void OpenGitWindow::ShowCommit()
{
	const Locator requested = Locator::GitCommit(fRepository->Text(), fRevision->Text(), "");
	try {
		ShowDirectory(Providers::Default().Get(requested).Resolve(requested));
	} catch (const ProviderError &error) {
		SetStatus(error.what());
	}
}


void OpenGitWindow::ShowDirectory(const Locator &directory)
{
	std::vector<DirEntry> entries;
	try {
		entries = Providers::Default().Get(directory).List(directory);
	} catch (const ProviderError &error) {
		SetStatus(error.what());
		return;
	}
	// Directories first.
	std::stable_partition(entries.begin(), entries.end(), [](const DirEntry &entry) {
		return entry.directory;
	});
	fDirectory = directory;
	fEntries = std::move(entries);

	for (int32 i = fList->CountItems() - 1; i >= 0; i--) {
		delete fList->RemoveItem(i);
	}
	if (!directory.Path().empty()) {
		fList->AddItem(new BStringItem(".."));
	}
	for (const DirEntry &entry : fEntries) {
		fList->AddItem(new BStringItem((entry.name + (entry.directory ? "/" : "")).c_str()));
	}
	fList->Select(0);
	fPlace->SetText((Locator::File(directory.Repository()).Leaf() + " @ " + directory.Revision().substr(0, 10) + ": /"
		+ directory.Path()).c_str());
	SetStatus("");
}


void OpenGitWindow::Invoke()
{
	int32 index = fList->CurrentSelection();
	if (!fDirectory.has_value() || index < 0) {
		return;
	}
	if (!fDirectory->Path().empty()) {
		if (index == 0) {
			ShowDirectory(fDirectory->Parent());
			return;
		}
		index--;
	}
	if (index >= static_cast<int32>(fEntries.size())) {
		return;
	}
	const DirEntry &entry = fEntries[index];
	const std::string path = fDirectory->Path().empty() ? entry.name : fDirectory->Path() + "/" + entry.name;
	if (entry.directory) {
		ShowDirectory(fDirectory->WithPath(path));
		return;
	}
	BMessage message(kMsgOpenLocator);
	message.AddString("locator", fDirectory->WithPath(path).ToString().c_str());
	fTarget.SendMessage(&message);
	PostMessage(B_QUIT_REQUESTED);
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
		case kMsgOpen:
			Invoke();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // Studio
