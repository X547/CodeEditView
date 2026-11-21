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
#include <CodeEditView/EditorCommands.h>
#include <CodeEditView/TextCommands.h>

#include "Ask.h"
#include "ChangeView.h"
#include "CodeEditLink.h"
#include "FindCommands.h"
#include "MessageEditor.h"
#include "Messages.h"
#include "PathMenu.h"
#include "Progress.h"


namespace GitDiff {


// Delay before refreshing after the window was activated or the index
// changed; changes come in bursts.
static constexpr bigtime_t kRefreshDelay = 300000;
static constexpr uint32 kMsgLoadFirst = 'GDlf';


// CodeEdit shows files of the working tree and of commits, not of the
// index.
static bool OpensInCodeEdit(Side side)
{
	return side != Side::Index;
}


// The key of a file in the list: its section and path.
static std::string FileKey(const std::string &path, DiffKind kind)
{
	return (kind == DiffKind::Unstaged ? "u:" : "s:") + path;
}


static std::optional<std::pair<std::string, DiffKind>> ParseFileKey(const std::optional<std::string> &key)
{
	if (!key.has_value() || key->size() < 2) {
		return std::nullopt;
	}
	return std::pair(key->substr(2), (*key)[0] == 'u' ? DiffKind::Unstaged : DiffKind::Staged);
}


// Changed files in two sections, like Git GUI, conflicted files in the
// unstaged one; files opened from the path menu without unstaged changes in
// a third, as unstaged (index against working tree).
static std::vector<ChangeSection> FileSections(const std::vector<StatusEntry> &entries,
	const std::set<std::string> &opened)
{
	std::vector<ChangeSection> sections(2);
	sections[0].title = "Unstaged changes";
	sections[1].title = "Staged changes";
	std::set<std::string> unstaged;
	for (const StatusEntry &entry : entries) {
		if (entry.conflicted || entry.unstaged != ' ') {
			const char letter = entry.conflicted ? 'U' : entry.unstaged;
			sections[0].items.push_back({FileKey(entry.path, DiffKind::Unstaged),
				std::string(1, letter) + "   " + entry.path});
			unstaged.insert(entry.path);
		}
	}
	for (const StatusEntry &entry : entries) {
		if (entry.staged != ' ' && !entry.conflicted) {
			sections[1].items.push_back({FileKey(entry.path, DiffKind::Staged),
				std::string(1, entry.staged) + "   " + entry.path});
		}
	}
	ChangeSection others;
	others.title = "Other files";
	for (const std::string &path : opened) {
		if (unstaged.count(path) == 0) {
			others.items.push_back({FileKey(path, DiffKind::Unstaged), "    " + path});
		}
	}
	if (!others.items.empty()) {
		sections.push_back(std::move(others));
	}
	return sections;
}


DiffWindow::DiffWindow(std::unique_ptr<Repository> repository, const std::string &relativePath):
	BWindow(
		BRect(50, 50, 989, 709),
		"Git diff",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRepository(std::move(repository))
{
	fStatusLoader = std::make_unique<DiffKit::BackgroundLoader<std::pair<uint64, std::vector<StatusEntry>>>>(
		BMessenger(this), kMsgStatusLoaded);
	fChangeView = new ChangeView("changes", MessageMode::Editable, 8);
	fChangeView->ShareTopBorder();
	fDiffView = fChangeView->Diff();
	fMessage = fChangeView->Message();

	BMenu *fileMenu = nullptr;
	BMenu *editMenu = nullptr;
	BMenu *searchMenu = nullptr;
	BMenu *changesMenu = nullptr;
	BMenu *viewMenu = nullptr;
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.GetMenu(fileMenu)
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
			.GetMenu(editMenu)
		.End()
		.AddMenu("Search")
			.GetMenu(searchMenu)
		.End()
		.AddMenu("Changes")
			.AddItem("Stage/unstage chunk or selected lines", kMsgStage, 'T')
			.AddItem("Revert chunk or selected lines", kMsgRevert, 'R')
			.AddItem("Stage/unstage file", kMsgStageFile, 'T', B_SHIFT_KEY)
			.AddItem("Commit staged changes", kMsgCommit, 'K')
			.AddSeparator()
			.GetMenu(changesMenu)
		.End()
		.AddMenu("View")
			.GetMenu(viewMenu)
		.End();
	fChangeView->AddNavigationItems(changesMenu);
	fChangeView->AddViewItems(viewMenu);
	CodeEdit::EditorCommands::AddEditItems(editMenu);
	editMenu->AddSeparatorItem();
	CodeEdit::TextCommands::AddItems(editMenu);
	FindKit::AddFindItems(searchMenu);

	// Any file of the working tree, after "Refresh".
	fileMenu->AddItem(new BMenuItem(new PathMenu("Show file", std::make_shared<FolderPathSource>(fRepository->Workdir()),
		std::string(), BMessage(kMsgShowPath), BMessenger(this))), 2);

	fAmend = new BCheckBox("amend", "Amend last commit", new BMessage(kMsgAmendToggled));
	fChangeId = new BCheckBox("changeId", "Add Change-Id", new BMessage(kMsgChangeIdToggled));
	fChangeId->SetToolTip("Add a Gerrit Change-Id to the message (gerrit.createChangeId in the repository settings)");
	BButton *commitButton = new BButton("commit", "Commit", new BMessage(kMsgCommit));
	BLayoutBuilder::Group<>(fChangeView->MessageOptionRow())
		.Add(fAmend)
		.Add(fChangeId)
		.AddGlue()
		.Add(commitButton);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frames of the list and the panes overlap the window borders.
			.SetInsets(-1, 0, -1, -1)
			.Add(fChangeView)
		.End();
	UpdateChangeId();

	// The index is replaced through index.lock, so the directory is watched.
	BEntry gitDir(fRepository->GitDir().c_str());
	if (gitDir.GetNodeRef(&fGitDirNode) == B_OK) {
		watch_node(&fGitDirNode, B_WATCH_DIRECTORY, this);
	}

	// The first status selects the file.
	fInitialPath = relativePath;
	// Once the window is shown.
	PostMessage(kMsgLoadFirst);
	UpdateTitle();
	MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
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
	const std::optional<std::pair<std::string, DiffKind>> selected = ParseFileKey(fChangeView->SelectedKey());
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


void DiffWindow::ShowPath(const std::string &path)
{
	if (path.empty()) {
		return;
	}
	fOpened.insert(path);
	fPendingSelect = FileKey(path, DiffKind::Unstaged);
	Refresh();
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
		for (int pane = 0; pane < 2; pane++) {
			fDiffView->SetLabel(pane, "");
			fDiffView->SetLabelLink(pane, false);
		}
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
		fDiffView->SetLabelLink(pane, OpensInCodeEdit(side) && fDocument->Exists(side));
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


void DiffWindow::OpenInCodeEdit(int pane)
{
	if (fDocument == nullptr || pane < 0 || pane > 1) {
		return;
	}
	const Side side = SideOf(fKind, pane);
	if (!OpensInCodeEdit(side) || !fDocument->Exists(side)) {
		return;
	}
	const CodeEdit::TextPoint caret = fDiffView->View(pane)->CaretLocation();
	const int32 line = static_cast<int32>(caret.line + 1);
	const int32 column = static_cast<int32>(caret.column + 1);
	if (side == Side::Worktree) {
		std::string path = fRepository->Workdir();
		if (!path.empty() && path.back() != '/') {
			path += '/';
		}
		path += fDocument->Path();
		DiffKit::OpenFileInCodeEdit(path.c_str(), line, column);
		return;
	}
	BMessage message(DiffKit::kMsgCodeEditOpenGitFile);
	message.AddString("repository", fRepository->Workdir().c_str());
	message.AddString("commit", "HEAD");
	message.AddString("path", fDocument->Path().c_str());
	message.AddInt32("line", line);
	message.AddInt32("column", column);
	DiffKit::SendToCodeEdit(message);
}


void DiffWindow::Refresh()
{
	fRefreshRunner.reset();
	const std::string workdir = fRepository->Workdir();
	const uint64 request = ++fStatusRequest;
	fStatusLoader->Load([workdir, request]() {
		Repository repository(workdir);
		return std::make_pair(request, repository.Status());
	});
}


void DiffWindow::LoadFirst()
{
	const std::string workdir = fRepository->Workdir();
	const uint64 request = ++fStatusRequest;
	std::shared_ptr<std::vector<StatusEntry>> status = std::make_shared<std::vector<StatusEntry>>();
	DiffKit::RunWithProgress(this, "Reading the changes", [workdir, status](DiffKit::Progress &progress) {
		(void)progress;
		Repository repository(workdir);
		*status = repository.Status();
	}, [this, request, status](const std::string &error) {
		if (!error.empty()) {
			ShowError(error.c_str());
			return;
		}
		ShowStatus(request, *status);
	});
}


void DiffWindow::StatusLoaded()
{
	std::string error;
	std::optional<std::pair<uint64, std::vector<StatusEntry>>> loaded = fStatusLoader->Take(error);
	if (!loaded.has_value()) {
		if (!error.empty()) {
			fprintf(stderr, "Git diff: %s\n", error.c_str());
		}
		return;
	}
	ShowStatus(loaded->first, loaded->second);
}


void DiffWindow::ShowStatus(uint64 request, const std::vector<StatusEntry> &status)
{
	if (request < fShownRequest) {
		return;
	}
	fShownRequest = request;
	fChangeView->SetSections(FileSections(status, fOpened));
	if (fFirstStatus) {
		fFirstStatus = false;
		const bool shown = !fInitialPath.empty()
			&& (fChangeView->Select(FileKey(fInitialPath, DiffKind::Unstaged))
				|| fChangeView->Select(FileKey(fInitialPath, DiffKind::Staged)));
		if (!shown && !fChangeView->SelectFirst(0)) {
			fChangeView->SelectFirst(1);
		}
	}
	if (fPendingSelect.has_value()) {
		fChangeView->Select(*fPendingSelect);
		fPendingSelect.reset();
	}

	std::set<std::string> listed = fOpened;
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
		if (MessageText(fMessage).empty()) {
			SetMessageText(fMessage, headMessage);
		}
	} else if (headMessage == MessageText(fMessage)) {
		SetMessageText(fMessage, std::string());
	}
}


// The setting can also change outside, or with Gerrit's hook installed.
void DiffWindow::UpdateChangeId()
{
	try {
		fChangeId->SetValue(fRepository->CreatesChangeId() ? B_CONTROL_ON : B_CONTROL_OFF);
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
}


void DiffWindow::ChangeIdToggled()
{
	try {
		fRepository->SetCreatesChangeId(fChangeId->Value() == B_CONTROL_ON);
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	UpdateChangeId();
}


// Commits the index; staged versions edited here are written first.
void DiffWindow::Commit()
{
	bool indexModified = false;
	for (const auto &[path, document] : fDocuments) {
		indexModified = indexModified || document->IsModified(Side::Index);
	}
	if (!indexModified) {
		CommitNow(false);
		return;
	}
	BAlert *alert = new BAlert("Git diff", "Some staged versions are edited but not written to the index. "
		"Write them and commit?", "Cancel", "Write and commit", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	DiffKit::Ask(this, alert, [this](int32 button) {
		if (button == 1) {
			CommitNow(true);
		}
	});
}


void DiffWindow::CommitNow(bool writeIndex)
{
	try {
		if (writeIndex) {
			for (const auto &[path, document] : fDocuments) {
				if (document->IsModified(Side::Index)) {
					document->WriteIndex(*fRepository);
				}
			}
		}
		fRepository->Commit(MessageText(fMessage), fAmend->Value() == B_CONTROL_ON);
	} catch (const std::exception &e) {
		ShowError(e.what());
		Refresh();
		return;
	}
	SetMessageText(fMessage, std::string());
	fAmend->SetValue(B_CONTROL_OFF);
	Refresh();
}


bool DiffWindow::HasModifiedDocuments() const
{
	for (const auto &[path, document] : fDocuments) {
		if (document->IsModified(Side::Index) || document->IsModified(Side::Worktree)) {
			return true;
		}
	}
	return false;
}


void DiffWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (active) {
		CodeEdit::EditorCommands::SetActiveHost(this);
		UpdateChangeId();
		ScheduleRefresh();
	}
}


CodeEditView *DiffWindow::TargetEditor()
{
	if (CodeEditView *focused = CodeEdit::EditorCommands::FocusedEditor(this)) {
		return focused;
	}
	return fDiffView->FocusedView();
}


void DiffWindow::MenusBeginning()
{
	CodeEdit::EditorCommands::UpdateMenu(KeyMenuBar(), TargetEditor());
	BWindow::MenusBeginning();
}


// With modified files, the window stays open and asks; the answer closes
// it, or asks the application to quit again if the request came from there.
bool DiffWindow::QuitRequested()
{
	if (!fCloseConfirmed && HasModifiedDocuments()) {
		if (!fAskingClose) {
			fAskingClose = true;
			// The application asks its windows from its own thread.
			const bool fromApplication = find_thread(nullptr) != Thread();
			BAlert *alert = new BAlert("Git diff", "Write the modified index and working tree files before closing?",
				"Cancel", "Discard", "Write", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			DiffKit::Ask(this, alert, [this, fromApplication](int32 button) {
				fAskingClose = false;
				if (button != 1 && button != 2) {
					return;
				}
				if (button == 2) {
					Save(true);
				}
				fCloseConfirmed = true;
				if (fromApplication) {
					be_app->PostMessage(B_QUIT_REQUESTED);
				} else {
					PostMessage(B_QUIT_REQUESTED);
				}
			});
		}
		return false;
	}
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void DiffWindow::MessageReceived(BMessage *message)
{
	if (fChangeView->HandleCommand(message)) {
		return;
	}
	switch (message->what) {
		case kMsgOpenRepository:
			be_app->PostMessage(message->what);
			break;
		case kMsgQuitApplication:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		case kMsgChangeViewFileSelected:
			ShowSelectedFile();
			break;
		case kMsgChangeViewFileInvoked:
			ShowSelectedFile();
			StageFile();
			break;
		case kMsgShowPath:
			ShowPath(message->GetString("path", ""));
			break;
		case kMsgRefresh:
		case kMsgRefreshTimer:
		case kMsgStatusChanged:
			Refresh();
			break;
		case kMsgStatusLoaded:
			StatusLoaded();
			break;
		case kMsgLoadFirst:
			LoadFirst();
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
		case DiffKit::kMsgPaneLabelClicked:
			OpenInCodeEdit(message->GetInt32("pane", 0));
			break;
		case kMsgStage:
			// To the index: left in the unstaged diff, right in the staged one.
			if (CanEdit()) {
				fDiffView->CopyChanges(fKind == DiffKind::Unstaged ? 0 : 1,
					fKind == DiffKind::Unstaged ? "Stage change" : "Unstage change");
			}
			break;
		case kMsgRevert:
			if (CanEdit() && fKind == DiffKind::Unstaged && fDocument->Exists(Side::Worktree)) {
				fDiffView->CopyChanges(1, "Revert change");
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
		case kMsgChangeIdToggled:
			ChangeIdToggled();
			break;
		default:
			if (!CodeEdit::EditorCommands::Dispatch(message, TargetEditor())) {
				BWindow::MessageReceived(message);
			}
			break;
	}
}


} // GitDiff
