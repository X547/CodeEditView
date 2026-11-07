#include "DiffWindow.h"

#include <stdio.h>

#include <exception>
#include <set>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextView.h>

#include <CodeEditView/CodeEditView.h>

#include "FileListView.h"
#include "Messages.h"


namespace GitDiff {


// Delay before refreshing after the window was activated or the index
// changed; changes come in bursts.
static constexpr bigtime_t kRefreshDelay = 300000;


DiffWindow::DiffWindow(std::unique_ptr<Repository> repository, const std::string &relativePath):
	BWindow(
		BRect(80, 80, 1279, 799),
		"Git diff",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRepository(std::move(repository))
{
	BMenu *algorithmMenu = nullptr;
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Open repository" B_UTF8_ELLIPSIS, kMsgOpenRepository, 'O')
			.AddItem("Refresh", kMsgRefresh, 'R', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Save", kMsgSave, 'S')
			.AddItem("Save all", kMsgSaveAll, 'S', B_SHIFT_KEY)
			.AddItem("Reload from disk", kMsgReload)
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
			.AddItem("Quit", kMsgQuitApplication, 'Q')
		.End()
		.AddMenu("Edit")
			.AddItem("Undo", B_UNDO, 'Z')
			.AddItem("Redo", B_REDO, 'Z', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Cut", B_CUT, 'X')
			.AddItem("Copy", B_COPY, 'C')
			.AddItem("Paste", B_PASTE, 'V')
			.AddSeparator()
			.AddItem("Select all", B_SELECT_ALL, 'A')
		.End()
		.AddMenu("Changes")
			.AddItem("Stage/unstage chunk or selected lines", kMsgStage, 'T')
			.AddItem("Revert chunk or selected lines", kMsgRevert, 'R')
			.AddItem("Stage/unstage file", kMsgStageFile, 'T', B_SHIFT_KEY)
			.AddItem("Commit staged changes", kMsgCommit, 'K')
			.AddSeparator()
			.AddItem("Next change", kMsgNextChunk, B_DOWN_ARROW, B_SHIFT_KEY)
			.AddItem("Previous change", kMsgPreviousChunk, B_UP_ARROW, B_SHIFT_KEY)
		.End()
		.AddMenu("View")
			.AddItem("Synchronize scrolling", kMsgToggleSyncScrolling)
				.GetItem(fSyncScrollingItem)
			.AddItem("Hide unchanged lines", kMsgToggleHideUnchanged)
				.GetItem(fHideUnchangedItem)
			.AddItem("Word wrap", kMsgToggleWordWrap)
				.GetItem(fWordWrapItem)
			.AddSeparator()
			.AddMenu("Diff algorithm")
				.GetMenu(algorithmMenu)
			.End()
		.End();

	static const char *kAlgorithmNames[] = {"Myers", "Minimal", "Patience"};
	for (int i = 0; i < 3; i++) {
		BMessage *message = new BMessage(kMsgSetAlgorithm);
		message->AddInt32("algorithm", i);
		fAlgorithmItems[i] = new BMenuItem(kAlgorithmNames[i], message);
		algorithmMenu->AddItem(fAlgorithmItems[i]);
	}
	algorithmMenu->SetRadioMode(true);
	fAlgorithmItems[0]->SetMarked(true);
	fSyncScrollingItem->SetMarked(true);
	fHideUnchangedItem->SetMarked(true);

	fFileList = new FileListView();
	fMessage = new BTextView("message");
	fMessage->SetWordWrap(true);
	fMessage->SetDoesUndo(true);
	BScrollView *messageScroll = new BScrollView("messageScroll", fMessage, 0, false, true);
	messageScroll->SetExplicitMinSize(BSize(B_SIZE_UNSET, 60));
	fAmend = new BCheckBox("amend", "Amend last commit", new BMessage(kMsgAmendToggled));
	BButton *commitButton = new BButton("commit", "Commit", new BMessage(kMsgCommit));
	BGroupView *commitBox = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(commitBox)
		.SetInsets(B_USE_SMALL_SPACING, B_USE_SMALL_SPACING, B_USE_SMALL_SPACING, B_USE_SMALL_SPACING)
		.Add(new BStringView("messageLabel", "Commit message:"))
		.Add(messageScroll)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fAmend)
			.AddGlue()
			.Add(commitButton)
		.End();
	BSplitView *leftSplit = new BSplitView(B_VERTICAL, 0);
	BLayoutBuilder::Split<>(leftSplit)
		.Add(new BScrollView("fileScroll", fFileList, 0, false, true, B_PLAIN_BORDER), 3)
		.Add(commitBox, 1);
	fDiffView = new DiffView();
	BSplitView *splitView = new BSplitView(B_HORIZONTAL, 0);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frames of the file list and the panes overlap the window
			// borders. The split view starts below the menu bar, so its
			// splitters do not cover the menu bar border; its items start one
			// pixel higher, which hides the top frame line of the file list.
			.SetInsets(-1, 0, -1, -1)
			.Add(splitView)
		.End();
	splitView->SetInsets(0, -1, 0, 0);
	splitView->AddChild(leftSplit, 1);
	splitView->AddChild(fDiffView, 4);

	// The index is replaced through index.lock, so the directory is watched.
	BEntry gitDir(fRepository->GitDir().c_str());
	if (gitDir.GetNodeRef(&fGitDirNode) == B_OK) {
		watch_node(&fGitDirNode, B_WATCH_DIRECTORY, this);
	}

	// The selection message shows the file once the window is laid out.
	Refresh();
	const bool shown = !relativePath.empty()
		&& (fFileList->SelectFile(relativePath, DiffKind::Unstaged)
			|| fFileList->SelectFile(relativePath, DiffKind::Staged));
	if (!shown && !fFileList->SelectFirstFile(DiffKind::Unstaged)) {
		fFileList->SelectFirstFile(DiffKind::Staged);
	}
	UpdateTitle();
}


DiffWindow::~DiffWindow()
{
	stop_watching(this);
}


FileDocument *DiffWindow::DocumentFor(const std::string &path)
{
	const auto it = fDocuments.find(path);
	if (it != fDocuments.end()) {
		return it->second.Get();
	}
	BReference<FileDocument> document(new FileDocument(path), true);
	try {
		document->Load(*fRepository);
	} catch (const std::exception &e) {
		ShowError(e.what());
		return nullptr;
	}
	fDocuments[path] = document;
	return document.Get();
}


void DiffWindow::ShowSelectedFile()
{
	const std::optional<std::pair<std::string, DiffKind>> selected = fFileList->Selected();
	if (!selected.has_value()) {
		return;
	}
	FileDocument *document = DocumentFor(selected->first);
	if (document != nullptr) {
		fDocument.SetTo(document);
		fKind = selected->second;
		// The state first, so the views get their read-only settings before
		// the models.
		UpdateDiffState();
		fDiffView->SetModels(document->Model(SideOf(fKind, 0)), document->Model(SideOf(fKind, 1)),
			document->Path() + (fKind == DiffKind::Unstaged ? ":unstaged" : ":staged"));
	}
	UpdateTitle();
}


bool DiffWindow::CanEdit() const
{
	return fDocument != nullptr && fDocument->ReadOnlyReason().empty();
}


// Labels, the read-only state of the right pane, and the arrows: they copy
// to the index (stage in the unstaged diff, unstage in the staged diff).
void DiffWindow::UpdateDiffState()
{
	static const char *kSideNames[] = {"HEAD", "Index", "Working tree"};
	static const char *kMissing[] = {"not in HEAD", "not in index", "deleted"};

	if (fDocument == nullptr) {
		fDiffView->SetLabel(0, "");
		fDiffView->SetLabel(1, "");
		fDiffView->SetCopyTargets(false, false);
		return;
	}
	for (int pane = 0; pane < 2; pane++) {
		const Side side = SideOf(fKind, pane);
		const int sideIndex = static_cast<int>(side);
		std::string text = kSideNames[sideIndex];
		if (!fDocument->Exists(side)) {
			text += std::string(" — ") + kMissing[sideIndex];
		}
		if (side != Side::Head && fDocument->IsModified(side)) {
			text += " — modified";
		}
		if (fDocument->ChangedOnDisk(side)) {
			text += " — changed on disk";
		}
		if (pane == 1 && !fDocument->ReadOnlyReason().empty()) {
			text += " — read-only: file " + fDocument->ReadOnlyReason();
		}
		fDiffView->SetLabel(pane, text.c_str());
	}
	fDiffView->SetReadOnly(0, true);
	fDiffView->SetReadOnly(1, !CanEdit() || (SideOf(fKind, 1) == Side::Worktree && !fDocument->Exists(Side::Worktree)));
	fDiffView->SetCopyTargets(fKind == DiffKind::Unstaged && CanEdit(), fKind == DiffKind::Staged && CanEdit());
}


// Copying into the index writes it at once; into the working tree buffer, it
// is saved with Save.
void DiffWindow::ChangesCopied(int pane)
{
	if (fDocument != nullptr && SideOf(fKind, pane) == Side::Index) {
		try {
			fDocument->WriteIndex(*fRepository);
		} catch (const std::exception &e) {
			ShowError(e.what());
		}
		Refresh();
	}
	UpdateDiffState();
}


void DiffWindow::Refresh()
{
	fRefreshRunner.reset();
	std::vector<StatusEntry> status;
	try {
		status = fRepository->Status();
	} catch (const std::exception &e) {
		fprintf(stderr, "Git diff: %s\n", e.what());
	}
	fFileList->SetStatus(status);

	std::set<std::string> listed;
	for (const StatusEntry &entry : status) {
		listed.insert(entry.path);
	}
	FileDocument *current = fDocument.Get();
	for (auto it = fDocuments.begin(); it != fDocuments.end();) {
		FileDocument *document = it->second.Get();
		try {
			document->Load(*fRepository);
		} catch (const std::exception &e) {
			fprintf(stderr, "Git diff: %s\n", e.what());
		}
		const bool keep = document == current || listed.count(it->first) > 0
			|| document->IsModified(Side::Index) || document->IsModified(Side::Worktree);
		it = keep ? std::next(it) : fDocuments.erase(it);
	}
	fDiffView->RefreshDiff();
	UpdateDiffState();
}


void DiffWindow::ScheduleRefresh()
{
	BMessage message(kMsgRefreshTimer);
	fRefreshRunner = std::make_unique<BMessageRunner>(BMessenger(this), &message, kRefreshDelay, 1);
}


void DiffWindow::UpdateTitle()
{
	std::string title = BPath(fRepository->Workdir().c_str()).Leaf();
	if (const FileDocument *document = fDocument.Get()) {
		title += " — " + document->Path();
		title += fKind == DiffKind::Unstaged ? " (unstaged)" : " (staged)";
	}
	SetTitle(title.c_str());
}


void DiffWindow::Save(bool all)
{
	try {
		if (all) {
			for (const auto &[path, document] : fDocuments) {
				if (document->IsModified(Side::Index)) {
					document->WriteIndex(*fRepository);
				}
				if (document->IsModified(Side::Worktree)) {
					document->SaveWorktree(*fRepository);
				}
			}
		} else if (FileDocument *document = fDocument.Get()) {
			if (fKind == DiffKind::Staged) {
				document->WriteIndex(*fRepository);
			} else if (document->IsModified(Side::Worktree)) {
				document->SaveWorktree(*fRepository);
			}
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	UpdateDiffState();
	Refresh();
}


void DiffWindow::ReloadDocument()
{
	FileDocument *document = fDocument.Get();
	if (document == nullptr) {
		return;
	}
	try {
		document->Load(*fRepository, true);
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	fDiffView->RefreshDiff();
	UpdateDiffState();
}


void DiffWindow::StageFile()
{
	FileDocument *document = fDocument.Get();
	if (document == nullptr || !document->ReadOnlyReason().empty()) {
		return;
	}
	try {
		if (fKind == DiffKind::Unstaged) {
			document->StageFile(*fRepository);
		} else {
			document->UnstageFile(*fRepository);
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	Refresh();
}


// Amending starts from the message of the commit being replaced.
void DiffWindow::AmendToggled()
{
	const std::string headMessage = fRepository->HeadMessage();
	if (fAmend->Value() == B_CONTROL_ON) {
		if (fMessage->TextLength() == 0) {
			fMessage->SetText(headMessage.c_str());
		}
	} else if (headMessage == fMessage->Text()) {
		fMessage->SetText("");
	}
}


// Commits the index; staged versions edited here are written first.
void DiffWindow::Commit()
{
	bool indexModified = false;
	for (const auto &[path, document] : fDocuments) {
		indexModified = indexModified || document->IsModified(Side::Index);
	}
	try {
		if (indexModified) {
			BAlert *alert = new BAlert("Git diff", "Some staged versions are edited but not written to the index. "
				"Write them and commit?", "Cancel", "Write and commit", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			if (alert->Go() != 1) {
				return;
			}
			for (const auto &[path, document] : fDocuments) {
				if (document->IsModified(Side::Index)) {
					document->WriteIndex(*fRepository);
				}
			}
		}
		fRepository->Commit(fMessage->Text(), fAmend->Value() == B_CONTROL_ON);
	} catch (const std::exception &e) {
		ShowError(e.what());
		Refresh();
		return;
	}
	fMessage->SetText("");
	fAmend->SetValue(B_CONTROL_OFF);
	Refresh();
}


bool DiffWindow::ConfirmClose()
{
	bool modified = false;
	for (const auto &[path, document] : fDocuments) {
		modified = modified || document->IsModified(Side::Index) || document->IsModified(Side::Worktree);
	}
	if (!modified) {
		return true;
	}
	BAlert *alert = new BAlert("Git diff", "Write the modified index and working tree files before closing?",
		"Cancel", "Discard", "Write", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	switch (alert->Go()) {
		case 1:
			return true;
		case 2:
			Save(true);
			return true;
		default:
			return false;
	}
}


void DiffWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (active) {
		ScheduleRefresh();
	}
}


bool DiffWindow::QuitRequested()
{
	if (!ConfirmClose()) {
		return false;
	}
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void DiffWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgOpenRepository:
			be_app->PostMessage(message->what);
			break;
		case kMsgQuitApplication:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		case kMsgFileSelected:
			ShowSelectedFile();
			break;
		case kMsgFileInvoked:
			ShowSelectedFile();
			StageFile();
			break;
		case kMsgRefresh:
		case kMsgRefreshTimer:
		case kMsgStatusChanged:
			Refresh();
			break;
		case B_NODE_MONITOR:
			ScheduleRefresh();
			break;
		case kMsgSave:
		case kMsgSaveAll:
			Save(message->what == kMsgSaveAll);
			break;
		case kMsgReload:
			ReloadDocument();
			break;
		case kMsgStage:
			// To the index: left in the unstaged diff, right in the staged one.
			if (CanEdit()) {
				fDiffView->CopyChanges(fKind == DiffKind::Unstaged ? 0 : 1);
			}
			break;
		case kMsgRevert:
			if (CanEdit() && fKind == DiffKind::Unstaged && fDocument->Exists(Side::Worktree)) {
				fDiffView->CopyChanges(1);
			}
			break;
		case kMsgChangesCopied:
			ChangesCopied(message->GetInt32("pane", 0));
			break;
		case kMsgDiffEdited:
			UpdateDiffState();
			break;
		case kMsgStageFile:
			StageFile();
			break;
		case kMsgCommit:
			Commit();
			break;
		case kMsgAmendToggled:
			AmendToggled();
			break;
		case kMsgNextChunk:
		case kMsgPreviousChunk:
			fDiffView->GoToChunk(message->what == kMsgNextChunk);
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
		case kMsgSetAlgorithm:
			fDiffView->SetAlgorithm(static_cast<DiffAlgorithm>(message->GetInt32("algorithm", 0)));
			break;
		case B_UNDO:
		case B_REDO:
		case B_CUT:
		case B_COPY:
		case B_PASTE:
		case B_SELECT_ALL: {
			BView *focus = CurrentFocus();
			if (dynamic_cast<CodeEditView*>(focus) != nullptr || dynamic_cast<BTextView*>(focus) != nullptr) {
				PostMessage(message, focus);
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // GitDiff
