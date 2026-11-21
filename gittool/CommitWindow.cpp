#include "CommitWindow.h"

#include <exception>

#include <Beep.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>

#include "ChangeView.h"
#include "CodeEditLink.h"
#include "CommitMessage.h"
#include "DiffView.h"
#include "DetailsView.h"
#include "Dialogs.h"
#include "Messages.h"
#include "Progress.h"
#include "WindowHelpers.h"


namespace GitTool {


using DiffKit::DiffView;
using DiffKit::TextSide;


static constexpr uint32 kMsgOpenInCodeEdit = 'CWoc';
static constexpr uint32 kMsgOpenParentInCodeEdit = 'CWop';
static constexpr uint32 kMsgLoadChanges = 'CWlc';


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
		BRect(50, 50, 989, 709),
		("Commit " + ShortId(id)).c_str(),
		B_TITLED_WINDOW_LOOK,
		feel,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRepositoryPath(repositoryPath),
	fRepository(OpenRepository(repositoryPath)),
	fDetails(ReadCommitDetails(fRepository.get(), id))
{
	fChangeView = new GitDiff::ChangeView("changes", GitDiff::MessageMode::ReadOnly, 10);
	fDiffView = fChangeView->Diff();
	fChangeView->ShareTopBorder();

	BMenu *viewMenu = nullptr;
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Open in CodeEdit", kMsgOpenInCodeEdit)
			.AddItem("Open parent version in CodeEdit", kMsgOpenParentInCodeEdit)
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
		.End()
		.AddMenu("Edit")
			.AddItem("Copy", B_COPY, 'C')
			.AddItem("Select all", B_SELECT_ALL, 'A')
		.End()
		.AddMenu("View")
			.GetMenu(viewMenu)
		.End();
	fChangeView->AddViewItems(viewMenu);
	viewMenu->AddSeparatorItem();
	fChangeView->AddNavigationItems(viewMenu);
	viewMenu->AddSeparatorItem();
	fParentMenu = new BMenu("Compare with");
	fParentMenu->SetRadioMode(true);
	viewMenu->AddItem(fParentMenu);

	fInfo = new DetailsView("details");
	fInfo->SetTarget(BMessenger(this));
	fChangeView->SetMessageHeader(fInfo);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frames overlap the window borders.
			.SetInsets(-1, 0, -1, -1)
			.Add(fChangeView)
		.End();

	ShowDetails();
	// Once the window is shown.
	PostMessage(kMsgLoadChanges);
	MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
}


void CommitWindow::ShowDetails()
{
	git_repository *repository = fRepository.get();
	SetTitle(("Commit " + ShortId(fDetails.id)).c_str());
	fParent = 0;
	while (BMenuItem *item = fParentMenu->RemoveItem(static_cast<int32>(0))) {
		delete item;
	}
	for (size_t i = 0; i < fDetails.parents.size(); i++) {
		BMessage *message = new BMessage(kMsgSetParent);
		message->AddInt32("parent", static_cast<int32>(i));
		const std::string label = "Parent " + std::to_string(i + 1) + ": "
			+ Describe(repository, fDetails.parents[i]);
		BMenuItem *item = new BMenuItem(label.c_str(), message);
		item->SetMarked(i == 0);
		fParentMenu->AddItem(item);
	}
	if (fDetails.parents.empty()) {
		BMenuItem *item = new BMenuItem("Empty tree (no parent)", nullptr);
		item->SetMarked(true);
		item->SetEnabled(false);
		fParentMenu->AddItem(item);
	}

	fInfo->Clear();
	fInfo->AddCommit("Commit", fDetails.id, OidString(fDetails.id), false);
	for (size_t i = 0; i < fDetails.parents.size(); i++) {
		const std::string label = fDetails.parents.size() > 1 ? "Parent " + std::to_string(i + 1) : "Parent";
		fInfo->AddCommit(label, fDetails.parents[i], Describe(repository, fDetails.parents[i]));
	}
	fInfo->AddPerson("Author", fDetails.author.name, fDetails.author.email, FormatTime(fDetails.author.time));
	if (fDetails.committer.name != fDetails.author.name || fDetails.committer.email != fDetails.author.email
		|| fDetails.committer.time != fDetails.author.time) {
		fInfo->AddPerson("Committer", fDetails.committer.name, fDetails.committer.email,
			FormatTime(fDetails.committer.time));
	}

	// The message, checked against the conventions of commit messages.
	CodeEditView *message = fChangeView->Message();
	{
		CodeEdit::ModelLock lock(*message->Model());
		message->Model()->SetText(fDetails.message);
	}
	message->SetDecorations(GitDiff::CommitMessageDecorations(fDetails.message));
}


void CommitWindow::ShowCommit(const git_oid &id)
{
	try {
		fDetails = ReadCommitDetails(fRepository.get(), id);
	} catch (const std::exception &e) {
		DiffKit::ShowError(e.what());
		return;
	}
	ShowDetails();
	LoadChanges();
}


void CommitWindow::LoadChanges()
{
	fDiffView->SetModels(nullptr, nullptr, std::string());
	fDiffView->SetLabelLink(0, false);
	fDiffView->SetLabelLink(1, false);
	fSides.clear();
	fChanges.clear();
	fChangeView->SetSections({});
	const uint64 request = ++fLoadRequest;
	const std::string path = fRepositoryPath;
	const git_oid id = fDetails.id;
	const size_t parent = fParent;
	std::shared_ptr<std::vector<FileChange>> changes = std::make_shared<std::vector<FileChange>>();
	DiffKit::RunWithProgress(this, "Reading the changes", [path, id, parent, changes](DiffKit::Progress &progress) {
		(void)progress;
		RepositoryPtr repository = OpenRepository(path);
		*changes = CommitChanges(repository.get(), id, parent);
	}, [this, request, changes](const std::string &error) {
		if (request != fLoadRequest) {
			return;
		}
		if (!error.empty()) {
			DiffKit::ShowError(error.c_str());
		}
		ShowChanges(std::move(*changes));
	});
}


void CommitWindow::ShowChanges(std::vector<FileChange> changes)
{
	fChanges = std::move(changes);
	GitDiff::ChangeSection section;
	for (size_t i = 0; i < fChanges.size(); i++) {
		const FileChange &change = fChanges[i];
		std::string label = std::string(1, change.status) + "   ";
		if (change.status == 'R' || change.status == 'C') {
			label += change.oldPath + " → " + change.newPath;
		} else {
			label += change.Path();
		}
		section.items.push_back({std::to_string(i), label});
	}
	fChangeView->SetSections({section});
	if (!fChanges.empty()) {
		fChangeView->SelectFirst();
	} else {
		fDiffView->SetLabel(0, "");
		fDiffView->SetLabel(1, "No changes");
	}
}


void CommitWindow::ShowSelectedFile()
{
	const std::optional<std::string> key = fChangeView->SelectedKey();
	if (!key.has_value()) {
		return;
	}
	const size_t index = std::stoul(*key);
	if (index >= fChanges.size()) {
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
	// The versions of commits open in CodeEdit.
	fDiffView->SetLabelLink(0, SelectedVersion(true).has_value());
	fDiffView->SetLabelLink(1, SelectedVersion(false).has_value());
	fDiffView->SetReadOnly(0, true);
	fDiffView->SetReadOnly(1, true);
	fDiffView->SetCopyTargets(false, false);
	fDiffView->SetModels(found->second.first->Model(), found->second.second->Model(),
		OidString(fDetails.id) + ":" + std::to_string(fParent) + ":" + change.Path());
}


const FileChange *CommitWindow::SelectedChange() const
{
	const std::optional<std::string> key = fChangeView->SelectedKey();
	if (!key.has_value()) {
		return nullptr;
	}
	const size_t index = std::stoul(*key);
	return index < fChanges.size() ? &fChanges[index] : nullptr;
}


// The commit and path of the selected file's version in the commit, or in
// the parent compared with; none if the file is not there.
std::optional<std::pair<git_oid, std::string>> CommitWindow::SelectedVersion(bool parent) const
{
	const FileChange *change = SelectedChange();
	if (change == nullptr) {
		return std::nullopt;
	}
	if (parent) {
		if (fParent >= fDetails.parents.size() || change->oldPath.empty()) {
			return std::nullopt;
		}
		return std::make_pair(fDetails.parents[fParent], change->oldPath);
	}
	if (change->newPath.empty()) {
		return std::nullopt;
	}
	return std::make_pair(fDetails.id, change->newPath);
}


// Shows the version in CodeEdit at the line of the caret in its pane.
void CommitWindow::OpenInCodeEdit(bool parent)
{
	const std::optional<std::pair<git_oid, std::string>> version = SelectedVersion(parent);
	if (!version.has_value()) {
		beep();
		return;
	}
	const char *workdir = git_repository_workdir(fRepository.get());
	BMessage message(DiffKit::kMsgCodeEditOpenGitFile);
	message.AddString("repository", workdir != nullptr ? workdir : git_repository_path(fRepository.get()));
	message.AddString("commit", OidString(version->first).c_str());
	message.AddString("path", version->second.c_str());
	CodeEditView *pane = fDiffView->View(parent ? 0 : 1);
	if (pane != nullptr) {
		const CodeEdit::TextPoint caret = pane->CaretLocation();
		message.AddInt32("line", static_cast<int32>(caret.line + 1));
		message.AddInt32("column", static_cast<int32>(caret.column + 1));
	}
	DiffKit::SendToCodeEdit(message);
}


void CommitWindow::MenusBeginning()
{
	BMenuBar *menuBar = KeyMenuBar();
	menuBar->FindItem(kMsgOpenInCodeEdit)->SetEnabled(SelectedVersion(false).has_value());
	menuBar->FindItem(kMsgOpenParentInCodeEdit)->SetEnabled(SelectedVersion(true).has_value());
	CodeEdit::EditorCommands::UpdateMenu(menuBar, TargetEditor());
	BWindow::MenusBeginning();
}


CodeEditView *CommitWindow::TargetEditor()
{
	if (CodeEditView *focused = CodeEdit::EditorCommands::FocusedEditor(this)) {
		return focused;
	}
	return fDiffView->FocusedView();
}


void CommitWindow::MessageReceived(BMessage *message)
{
	if (fChangeView->HandleCommand(message)) {
		return;
	}
	switch (message->what) {
		case kMsgOpenInCodeEdit:
		case kMsgOpenParentInCodeEdit:
			OpenInCodeEdit(message->what == kMsgOpenParentInCodeEdit);
			break;
		case GitDiff::kMsgChangeViewFileSelected:
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
		case kMsgLoadChanges:
			LoadChanges();
			break;
		case DiffKit::kMsgPaneLabelClicked:
			OpenInCodeEdit(message->GetInt32("pane", 0) == 0);
			break;
		case kMsgCommitLinkClicked: {
			const void *data = nullptr;
			ssize_t size = 0;
			if (message->FindData("commit", B_RAW_TYPE, &data, &size) != B_OK || size != sizeof(git_oid)) {
				break;
			}
			const git_oid commit = *static_cast<const git_oid*>(data);
			if (message->GetBool("replace", false)) {
				ShowCommit(commit);
				break;
			}
			try {
				CommitWindow *window = new CommitWindow(fRepositoryPath, commit, B_NORMAL_WINDOW_FEEL);
				DiffKit::PlaceOver(window, Frame());
				window->Show();
			} catch (const std::exception &e) {
				DiffKit::ShowError(e.what());
			}
			break;
		}
		default:
			if (!CodeEdit::EditorCommands::Dispatch(message, TargetEditor())) {
				BWindow::MessageReceived(message);
			}
			break;
	}
}


} // GitTool
