#include "CommitWindow.h"

#include <exception>

#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>

#include <CodeEditView/CodeEditView.h>

#include "ChangeView.h"
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
		BRect(50, 50, 989, 709),
		("Commit " + ShortId(id)).c_str(),
		B_TITLED_WINDOW_LOOK,
		feel,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRepository(OpenRepository(repositoryPath)),
	fDetails(ReadCommitDetails(fRepository.get(), id))
{
	fChangeView = new GitDiff::ChangeView("changes", GitDiff::MessageMode::ReadOnly, 10);
	fDiffView = fChangeView->Diff();
	fChangeView->ShareTopBorder();

	BMenu *parentMenu = nullptr;
	BMenu *viewMenu = nullptr;
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
			.GetMenu(viewMenu)
		.End();
	fChangeView->AddViewItems(viewMenu);
	viewMenu->AddSeparatorItem();
	fChangeView->AddNavigationItems(viewMenu);
	viewMenu->AddSeparatorItem();
	parentMenu = new BMenu("Compare with");
	viewMenu->AddItem(parentMenu);

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

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frames overlap the window borders.
			.SetInsets(-1, 0, -1, -1)
			.Add(fChangeView)
		.End();

	ShowHeader();
	LoadChanges();
	MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
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
	CodeEditView *header = fChangeView->Message();
	{
		CodeEdit::ModelLock lock(*header->Model());
		header->Model()->SetText(text);
	}
	std::vector<CodeEdit::Decoration> decorations = GitDiff::CommitMessageDecorations(fDetails.message);
	for (CodeEdit::Decoration &decoration : decorations) {
		decoration.start += messageStart;
		decoration.end += messageStart;
	}
	header->SetDecorations(std::move(decorations));
}


void CommitWindow::LoadChanges()
{
	fDiffView->SetModels(nullptr, nullptr, std::string());
	fSides.clear();
	fChangeView->SetSections({});
	try {
		fChanges = CommitChanges(fRepository.get(), fDetails.id, fParent);
	} catch (const std::exception &e) {
		fChanges.clear();
		DiffKit::ShowError(e.what());
	}
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
	fDiffView->SetReadOnly(0, true);
	fDiffView->SetReadOnly(1, true);
	fDiffView->SetCopyTargets(false, false);
	fDiffView->SetModels(found->second.first->Model(), found->second.second->Model(),
		OidString(fDetails.id) + ":" + std::to_string(fParent) + ":" + change.Path());
}


void CommitWindow::MessageReceived(BMessage *message)
{
	if (fChangeView->HandleCommand(message)) {
		return;
	}
	switch (message->what) {
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
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // GitTool
