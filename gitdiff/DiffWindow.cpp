#include "DiffWindow.h"

#include <stdio.h>

#include <exception>
#include <set>

#include <Alert.h>
#include <Application.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <ScrollView.h>
#include <SplitView.h>

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
	fDiffView = new DiffView(fRepository.get());
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
	splitView->AddChild(new BScrollView("fileScroll", fFileList, 0, false, true, B_PLAIN_BORDER), 1);
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
		fDiffView->SetDiff(document, selected->second);
	}
	UpdateTitle();
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
	FileDocument *current = fDiffView->Document();
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
	fDiffView->UpdateState();
}


void DiffWindow::ScheduleRefresh()
{
	BMessage message(kMsgRefreshTimer);
	fRefreshRunner = std::make_unique<BMessageRunner>(BMessenger(this), &message, kRefreshDelay, 1);
}


void DiffWindow::UpdateTitle()
{
	std::string title = BPath(fRepository->Workdir().c_str()).Leaf();
	if (const FileDocument *document = fDiffView->Document()) {
		title += " — " + document->Path();
		title += fDiffView->Kind() == DiffKind::Unstaged ? " (unstaged)" : " (staged)";
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
		} else if (FileDocument *document = fDiffView->Document()) {
			if (fDiffView->Kind() == DiffKind::Staged) {
				document->WriteIndex(*fRepository);
			} else if (document->IsModified(Side::Worktree)) {
				document->SaveWorktree(*fRepository);
			}
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	fDiffView->UpdateState();
	Refresh();
}


void DiffWindow::ReloadDocument()
{
	FileDocument *document = fDiffView->Document();
	if (document == nullptr) {
		return;
	}
	try {
		document->Load(*fRepository, true);
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	fDiffView->RefreshDiff();
	fDiffView->UpdateState();
}


void DiffWindow::StageFile()
{
	FileDocument *document = fDiffView->Document();
	if (document == nullptr || !document->ReadOnlyReason().empty()) {
		return;
	}
	try {
		if (fDiffView->Kind() == DiffKind::Unstaged) {
			document->StageFile(*fRepository);
		} else {
			document->UnstageFile(*fRepository);
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
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
			fDiffView->Stage();
			break;
		case kMsgRevert:
			fDiffView->Revert();
			break;
		case kMsgStageFile:
			StageFile();
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


} // GitDiff
