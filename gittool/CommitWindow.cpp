#include "CommitWindow.h"

#include <exception>

#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <SplitView.h>

#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>

#include "CommitMessage.h"
#include "DiffView.h"
#include "Dialogs.h"
#include "Messages.h"


namespace GitTool {


using DiffKit::DiffView;
using DiffKit::TextSide;


static std::string SignatureText(const Signature &signature)
{
	return signature.name + " <" + signature.email + ">, " + FormatTime(signature.time);
}


// What a pane shows for a file version: its text, or a note for binary
// files.
static std::optional<std::string> DisplayData(const std::optional<std::string> &data)
{
	if (!data.has_value() || !DiffKit::IsBinary(*data)) {
		return data;
	}
	return "(binary file, " + std::to_string(data->size()) + " bytes)\n";
}


CommitWindow::CommitWindow(const std::string &repositoryPath, const git_oid &id, window_feel feel):
	BWindow(
		BRect(100, 100, 1199, 799),
		("Commit " + ShortId(id)).c_str(),
		B_TITLED_WINDOW_LOOK,
		feel,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRepository(OpenRepository(repositoryPath)),
	fDetails(ReadCommitDetails(fRepository.get(), id))
{
	BMenu *parentMenu = nullptr;
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
		.End()
		.AddMenu("Edit")
			.AddItem("Copy", B_COPY, 'C')
			.AddItem("Select all", B_SELECT_ALL, 'A')
		.End()
		.AddMenu("View")
			.AddItem("Synchronize scrolling", kMsgToggleSyncScrolling)
				.GetItem(fSyncScrollingItem)
			.AddItem("Hide unchanged lines", kMsgToggleHideUnchanged)
				.GetItem(fHideUnchangedItem)
			.AddItem("Word wrap", kMsgToggleWordWrap)
				.GetItem(fWordWrapItem)
			.AddSeparator()
			.AddItem("Next change", kMsgNextChange, B_DOWN_ARROW, B_SHIFT_KEY)
			.AddItem("Previous change", kMsgPreviousChange, B_UP_ARROW, B_SHIFT_KEY)
			.AddSeparator()
			.AddMenu("Compare with")
				.GetMenu(parentMenu)
			.End()
		.End();
	fSyncScrollingItem->SetMarked(true);
	fHideUnchangedItem->SetMarked(true);

	parentMenu->SetRadioMode(true);
	for (size_t i = 0; i < fDetails.parents.size(); i++) {
		BMessage *message = new BMessage(kMsgSetParent);
		message->AddInt32("parent", static_cast<int32>(i));
		const std::string label = "Parent " + std::to_string(i + 1) + ": "
			+ Describe(fRepository.get(), fDetails.parents[i]);
		BMenuItem *item = new BMenuItem(label.c_str(), message);
		item->SetMarked(i == 0);
		parentMenu->AddItem(item);
	}
	if (fDetails.parents.empty()) {
		BMenuItem *item = new BMenuItem("Empty tree (no parent)", nullptr);
		item->SetMarked(true);
		item->SetEnabled(false);
		parentMenu->AddItem(item);
	}

	fHeaderModel.SetTo(new CodeEdit::TextModel(), true);
	fHeader = new CodeEditView("header", fHeaderModel.Get());
	CodeEdit::EditorSettings settings = GitDiff::CommitMessageSettings();
	settings.readOnly = true;
	fHeader->SetSettings(settings);
	fFiles = new BListView("files", B_SINGLE_SELECTION_LIST);
	fFiles->SetSelectionMessage(new BMessage(kMsgCommitFileSelected));
	fDiffView = new DiffView();

	CodeEditScrollView *headerScroll = new CodeEditScrollView("headerScroll", fHeader);
	headerScroll->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 120));
	BSplitView *filesSplit = new BSplitView(B_HORIZONTAL, 0);
	BLayoutBuilder::Split<>(filesSplit)
		.Add(new BScrollView("fileScroll", fFiles, 0, false, true, B_PLAIN_BORDER), 1)
		.Add(fDiffView, 4);
	BSplitView *mainSplit = new BSplitView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Split<>(mainSplit)
		.Add(headerScroll, 0)
		.Add(filesSplit, 1);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(-1, 0, -1, -1)
			.Add(mainSplit)
		.End();

	ShowHeader();
	LoadChanges();
}


void CommitWindow::ShowHeader()
{
	git_repository *repository = fRepository.get();
	std::string text = "Commit " + OidString(fDetails.id) + "\n";
	for (const git_oid &parent : fDetails.parents) {
		text += "Parent " + Describe(repository, parent) + "\n";
	}
	text += "Author " + SignatureText(fDetails.author) + "\n";
	if (fDetails.committer.name != fDetails.author.name || fDetails.committer.email != fDetails.author.email
		|| fDetails.committer.time != fDetails.author.time) {
		text += "Committer " + SignatureText(fDetails.committer) + "\n";
	}
	// The message follows the commit details; its lines are checked against
	// the conventions of commit messages.
	text += "\n";
	const size_t messageStart = text.size();
	text += fDetails.message;
	{
		CodeEdit::ModelLock lock(*fHeaderModel);
		fHeaderModel->SetText(text);
	}
	std::vector<CodeEdit::Decoration> decorations = GitDiff::CommitMessageDecorations(fDetails.message);
	for (CodeEdit::Decoration &decoration : decorations) {
		decoration.start += messageStart;
		decoration.end += messageStart;
	}
	fHeader->SetDecorations(std::move(decorations));
}


void CommitWindow::LoadChanges()
{
	fDiffView->SetModels(nullptr, nullptr, std::string());
	fSides.clear();
	for (int32 i = fFiles->CountItems() - 1; i >= 0; i--) {
		delete fFiles->RemoveItem(i);
	}
	try {
		fChanges = CommitChanges(fRepository.get(), fDetails.id, fParent);
	} catch (const std::exception &e) {
		fChanges.clear();
		DiffKit::ShowError(e.what());
	}
	for (const FileChange &change : fChanges) {
		std::string label = std::string(1, change.status) + "   ";
		if (change.status == 'R' || change.status == 'C') {
			label += change.oldPath + " → " + change.newPath;
		} else {
			label += change.Path();
		}
		fFiles->AddItem(new BStringItem(label.c_str()));
	}
	if (!fChanges.empty()) {
		fFiles->Select(0);
	} else {
		fDiffView->SetLabel(0, "");
		fDiffView->SetLabel(1, "No changes");
	}
}


void CommitWindow::ShowSelectedFile()
{
	const int32 index = fFiles->CurrentSelection();
	if (index < 0 || static_cast<size_t>(index) >= fChanges.size()) {
		return;
	}
	const FileChange &change = fChanges[index];
	auto found = fSides.find(index);
	if (found == fSides.end()) {
		Sides sides(std::make_unique<TextSide>(change.Path()), std::make_unique<TextSide>(change.Path()));
		try {
			sides.first->Load(DisplayData(ReadFileVersion(fRepository.get(), change.oldId, change.oldMode)), true);
			sides.second->Load(DisplayData(ReadFileVersion(fRepository.get(), change.newId, change.newMode)), true);
		} catch (const std::exception &e) {
			DiffKit::ShowError(e.what());
			return;
		}
		found = fSides.emplace(index, std::move(sides)).first;
	}

	const std::string parentName = fParent < fDetails.parents.size()
		? "Parent " + ShortId(fDetails.parents[fParent]) : std::string("Empty tree");
	fDiffView->SetLabel(0, (parentName + " — " + (change.oldPath.empty() ? "not present" : change.oldPath)).c_str());
	fDiffView->SetLabel(1, ("Commit " + ShortId(fDetails.id) + " — "
		+ (change.newPath.empty() ? "deleted" : change.newPath)).c_str());
	fDiffView->SetReadOnly(0, true);
	fDiffView->SetReadOnly(1, true);
	fDiffView->SetCopyTargets(false, false);
	fDiffView->SetModels(found->second.first->Model(), found->second.second->Model(),
		OidString(fDetails.id) + ":" + std::to_string(fParent) + ":" + change.Path());
}


void CommitWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgCommitFileSelected:
			ShowSelectedFile();
			break;
		case kMsgSetParent: {
			const size_t parent = static_cast<size_t>(message->GetInt32("parent", 0));
			if (parent != fParent) {
				fParent = parent;
				LoadChanges();
			}
			break;
		}
		case kMsgNextChange:
		case kMsgPreviousChange:
			fDiffView->GoToChunk(message->what == kMsgNextChange);
			break;
		case kMsgToggleSyncScrolling:
			fSyncScrollingItem->SetMarked(!fSyncScrollingItem->IsMarked());
			fDiffView->SetSyncScrolling(fSyncScrollingItem->IsMarked());
			break;
		case kMsgToggleHideUnchanged:
			fHideUnchangedItem->SetMarked(!fHideUnchangedItem->IsMarked());
			fDiffView->SetHideUnchanged(fHideUnchangedItem->IsMarked());
			break;
		case kMsgToggleWordWrap:
			fWordWrapItem->SetMarked(!fWordWrapItem->IsMarked());
			fDiffView->SetWordWrap(fWordWrapItem->IsMarked());
			break;
		case B_COPY:
		case B_SELECT_ALL: {
			BView *focus = CurrentFocus();
			if (dynamic_cast<CodeEditView*>(focus) != nullptr) {
				PostMessage(message, focus);
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // GitTool
