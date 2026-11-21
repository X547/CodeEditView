#include "StepEditor.h"

#include <exception>

#include <Alert.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <GroupView.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextControl.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>
#include <CodeEditView/TextCommands.h>
#include <CodeEditView/TextModel.h>

#include "Ask.h"
#include "ChangeView.h"
#include "CodeEditLink.h"
#include "CommitDiff.h"
#include "DiffView.h"
#include "FindCommands.h"
#include "MessageEditor.h"
#include "Messages.h"
#include "PathMenu.h"
#include "WindowHelpers.h"


namespace GitTool {


using DiffKit::ShowError;
using DiffKit::TextSide;


static const char *kVersionNames[3] = {
	"New base (ours)",
	"The commit (theirs)",
	"The commit's parent",
};


static std::optional<FileVersion> VersionOf(const git_oid &id, uint32_t mode)
{
	if (git_oid_is_zero(&id)) {
		return std::nullopt;
	}
	FileVersion version;
	version.id = id;
	version.mode = mode;
	return version;
}


static std::string Text(TextSide &side)
{
	return side.SavedData();
}


static bool HasConflictMarkers(const std::string &text)
{
	return text.starts_with("<<<<<<< ") || text.find("\n<<<<<<< ") != std::string::npos;
}


StepEditor::StepEditor(BWindow *history, const std::string &repositoryPath, const StepEditorInput &input):
	BWindow(
		BRect(0, 0, 939, 639),
		"",
		B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fTarget(history),
	fRepository(OpenRepository(repositoryPath)),
	fInput(input)
{
	DiffKit::AttachDialog(history, this);
	static const char *kTitles[] = {"Edit contents", "Split", "Resolve conflicts"};
	const std::string commitName = input.mode == StepEditMode::Resolve ? std::string("the commit")
		: ShortId(input.result);
	SetTitle((std::string(kTitles[static_cast<int>(input.mode)]) + " — " + commitName).c_str());

	if (input.mode == StepEditMode::Resolve) {
		for (const ConflictFile &conflict : input.conflicts) {
			AddFile(conflict.path, VersionOf(conflict.ours, conflict.oursMode),
				VersionOf(conflict.theirs, conflict.theirsMode), VersionOf(conflict.ancestor, conflict.ancestorMode));
		}
	} else {
		// A renamed file is two files here: the old path deleted, the new
		// one added.
		for (const FileChange &change : ChangesBetween(fRepository.get(), input.parent, input.result)) {
			std::vector<std::string> paths;
			if (!change.newPath.empty()) {
				paths.push_back(change.newPath);
			}
			if (!change.oldPath.empty() && change.oldPath != change.newPath) {
				paths.push_back(change.oldPath);
			}
			for (const std::string &path : paths) {
				AddFile(path, ReadPath(fRepository.get(), input.parent, path),
					ReadPath(fRepository.get(), input.result, path), std::nullopt);
			}
		}
	}

	// Split edits the message of the first part; the other modes keep the
	// commit's message.
	fChangeView = new GitDiff::ChangeView("changes",
		input.mode == StepEditMode::Split ? GitDiff::MessageMode::Editable : GitDiff::MessageMode::Hidden, 6);
	fDiffView = fChangeView->Diff();
	fDeleted = new BCheckBox("deleted", "Delete the file", new BMessage(kMsgDeleteToggled));
	fTakeLeft = new BButton("takeLeft", "Use the left version", new BMessage(kMsgTakeLeft));


	static const char *kHints[] = {
		"Edit the commit's version of its files (right). The arrows take changes back from the parent.",
		"The right side is the first part: take changes from the commit (left) with the arrows or by editing. "
			"The second part gets the rest.",
		"Edit the result (right) until no conflict markers remain, or use a whole version.",
	};
	BStringView *hint = new BStringView("hint", kHints[static_cast<int>(input.mode)]);
	hint->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	hint->SetExplicitMinSize(BSize(50, B_SIZE_UNSET));

	BGroupLayout *fileRow = fChangeView->ActionRow();
	BLayoutBuilder::Group<>(fileRow)
		.Add(fDeleted)
		.Add(fTakeLeft)
		.AddGlue();
	BTextControl *addPath = nullptr;
	if (input.mode == StepEditMode::Edit) {
		addPath = new BTextControl("addPath", "Other file:", "", new BMessage(kMsgAddFile));
		fileRow->AddView(addPath);
		fileRow->AddView(new BButton("add", "Add", new BMessage(kMsgAddFile)));
	}
	if (input.mode == StepEditMode::Resolve) {
		BPopUpMenu *leftMenu = new BPopUpMenu("left");
		for (int i = 0; i < 3; i++) {
			BMessage *message = new BMessage(kMsgLeftVersion);
			message->AddInt32("version", i);
			BMenuItem *item = new BMenuItem(kVersionNames[i], message);
			item->SetMarked(i == 0);
			leftMenu->AddItem(item);
		}
		fLeftField = new BMenuField("leftVersion", "Left pane:", leftMenu);
		fileRow->AddView(0, fLeftField);
	}

	BButton *cancel = new BButton("Cancel", new BMessage(kMsgDialogCancel));
	BButton *ok = new BButton(input.mode == StepEditMode::Split ? "Split" : "OK", new BMessage(kMsgDialogOk));
	BMenu *editMenu = nullptr;
	BMenu *searchMenu = nullptr;
	BMenu *changesMenu = nullptr;
	BMenu *viewMenu = nullptr;
	BMessage *toggleDeleted = new BMessage(kMsgDeleteToggled);
	toggleDeleted->AddBool("toggle", true);
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem(input.mode == StepEditMode::Split ? "Split" : "Apply to the commit", kMsgDialogOk, B_ENTER)
			.AddItem("Close without changes", kMsgDialogCancel, 'W')
		.End()
		.AddMenu("Edit")
			.GetMenu(editMenu)
		.End()
		.AddMenu("Search")
			.GetMenu(searchMenu)
		.End()
		.AddMenu("Changes")
			.AddItem("Take change or selected lines from the left", kMsgTakeChange, 'U')
			.AddItem("Use the left version of the file", kMsgTakeLeft)
			.AddItem("Delete the file", toggleDeleted)
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
	if (input.mode == StepEditMode::Edit) {
		// Any file of the commit or of its parent, as the "Other file" field.
		std::vector<git_oid> commits = {input.result};
		if (input.parent.has_value()) {
			commits.push_back(*input.parent);
		}
		try {
			BMenu *fileMenu = menuBar->ItemAt(0)->Submenu();
			fileMenu->AddItem(new BMenuItem(new GitDiff::PathMenu("Add file",
				std::make_shared<GitDiff::TreePathSource>(fRepository.get(), commits), std::string(),
				BMessage(kMsgAddFile), BMessenger(this))), 0);
			fileMenu->AddItem(new BSeparatorItem(), 1);
		} catch (const std::exception &e) {
			ShowError(e.what());
		}
	}
	if (input.mode == StepEditMode::Split) {
		std::string message = input.message;
		const size_t subjectEnd = message.find('\n');
		message.insert(subjectEnd == std::string::npos ? message.size() : subjectEnd, " (part 1)");
		fChangeView->SetMessage(message);
		fChangeView->SetMessageLabel("Message of the first part:");
	}

	// The frames of the list and the panes overlap the window borders.
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(hint)
		.End()
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(-1, 0, -1, 0)
			.Add(fChangeView, 1)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	SetDefaultButton(ok);
	DiffKit::KeepEnterInEditors(this);

	UpdateFileList();
	fChangeView->SelectFirst();
	DiffKit::PlaceOver(this, history->Frame());
}


std::optional<std::string> StepEditor::ReadVersion(const std::optional<FileVersion> &version)
{
	if (!version.has_value()) {
		return std::nullopt;
	}
	return ReadFileVersion(fRepository.get(), version->id, version->mode);
}


void StepEditor::AddFile(const std::string &path, const std::optional<FileVersion> &first,
	const std::optional<FileVersion> &second, const std::optional<FileVersion> &third)
{
	auto file = std::make_unique<File>();
	file->path = path;
	file->versions[0] = first;
	file->versions[1] = second;
	file->versions[2] = third;
	for (const std::optional<FileVersion> &version : file->versions) {
		if (version.has_value()) {
			file->mode = version->mode;
		}
	}
	file->left = std::make_unique<TextSide>(path);
	file->right = std::make_unique<TextSide>(path);
	LoadSides(*file);
	fFiles.push_back(std::move(file));
}


// The left and right versions, by mode; binary files and others that are
// not edited as text are taken whole.
void StepEditor::LoadSides(File &file)
{
	try {
		switch (fInput.mode) {
			case StepEditMode::Edit:
				file.left->Load(ReadVersion(file.versions[0]), true);
				file.right->Load(ReadVersion(file.versions[1]), true);
				file.taken = file.versions[1];
				break;
			case StepEditMode::Split:
				file.left->Load(ReadVersion(file.versions[1]), true);
				file.right->Load(ReadVersion(file.versions[0]), true);
				file.taken = file.versions[0];
				break;
			case StepEditMode::Resolve: {
				file.left->Load(ReadVersion(file.versions[file.leftVersion]), true);
				const std::optional<std::string> ours = ReadVersion(file.versions[0]);
				const std::optional<std::string> theirs = ReadVersion(file.versions[1]);
				const std::optional<std::string> ancestor = ReadVersion(file.versions[2]);
				git_merge_file_input inputs[3];
				const std::optional<std::string> *datas[] = {&ancestor, &ours, &theirs};
				for (int i = 0; i < 3; i++) {
					git_merge_file_input input = GIT_MERGE_FILE_INPUT_INIT;
					input.ptr = datas[i]->has_value() ? (*datas[i])->data() : nullptr;
					input.size = datas[i]->has_value() ? (*datas[i])->size() : 0;
					input.path = file.path.c_str();
					input.mode = file.mode;
					inputs[i] = input;
				}
				git_merge_file_options options = GIT_MERGE_FILE_OPTIONS_INIT;
				options.ancestor_label = "parent of the commit";
				options.our_label = "new base";
				options.their_label = "the commit";
				git_merge_file_result merged {};
				Check(git_merge_file(&merged, &inputs[0], &inputs[1], &inputs[2], &options), "Cannot merge the file");
				const std::string text(merged.ptr != nullptr ? merged.ptr : "", merged.len);
				git_merge_file_result_free(&merged);
				file.right->Load(text, true);
				file.taken = file.versions[0];
				break;
			}
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	file.wholeFile = !file.left->Problem().empty() || !file.right->Problem().empty();
	file.initiallyDeleted = fInput.mode != StepEditMode::Resolve && !file.right->Exists();
	file.deleted = file.initiallyDeleted;
	if (file.wholeFile) {
		const std::string note = "(" + file.path + " is taken as a whole: "
			+ (file.right->Problem().empty() ? file.left->Problem() : file.right->Problem()) + ")\n";
		file.right->Load(note, true);
	}
}


StepEditor::File *StepEditor::Selected() const
{
	const std::optional<std::string> key = fChangeView->SelectedKey();
	if (!key.has_value()) {
		return nullptr;
	}
	const size_t index = std::stoul(*key);
	return index < fFiles.size() ? fFiles[index].get() : nullptr;
}


void StepEditor::ShowSelected()
{
	File *file = Selected();
	if (file == nullptr) {
		fDiffView->SetModels(nullptr, nullptr, std::string());
		fDiffView->SetLabelLink(0, false);
		return;
	}
	static const char *kLeft[] = {"Parent", "The commit", ""};
	static const char *kRight[] = {"This commit (editable)", "First part (editable)", "Result (editable)"};
	const int mode = static_cast<int>(fInput.mode);
	const std::string left = fInput.mode == StepEditMode::Resolve ? kVersionNames[file->leftVersion] : kLeft[mode];
	fDiffView->SetLabel(0, (left + " — " + file->path).c_str());
	fDiffView->SetLabel(1, (std::string(kRight[mode]) + " — " + file->path
		+ (file->deleted ? " — deleted" : "")).c_str());
	fDiffView->SetLabelLink(0, LeftCommit().has_value() && file->left->Exists());
	fDiffView->SetReadOnly(0, true);
	fDiffView->SetReadOnly(1, file->wholeFile || file->deleted);
	fDiffView->SetCopyTargets(false, !file->wholeFile && !file->deleted);
	fDiffView->SetModels(file->left->Model(), file->right->Model(), file->path);
	fDeleted->SetValue(file->deleted ? B_CONTROL_ON : B_CONTROL_OFF);
	if (fLeftField != nullptr) {
		if (BMenuItem *item = fLeftField->Menu()->ItemAt(file->leftVersion)) {
			item->SetMarked(true);
		}
	}
}


// The marks of the files; ChangeView updates the items in place, so editing
// does not change the selection.
void StepEditor::UpdateFileList()
{
	GitDiff::ChangeSection section;
	for (size_t i = 0; i < fFiles.size(); i++) {
		const File &file = *fFiles[i];
		std::string mark = "    ";
		if (file.deleted) {
			mark = "✕  ";
		} else if (fInput.mode == StepEditMode::Resolve) {
			mark = !file.wholeFile && HasConflictMarkers(Text(*file.right)) ? "⚠  " : "✓  ";
		} else if (file.right->IsModified() || file.deleted != file.initiallyDeleted) {
			mark = "●  ";
		}
		section.items.push_back({std::to_string(i), mark + file.path});
	}
	fChangeView->SetSections({section});
}


std::optional<git_oid> StepEditor::LeftCommit() const
{
	switch (fInput.mode) {
		case StepEditMode::Edit:
			return fInput.parent;
		case StepEditMode::Split:
			return fInput.result;
		default:
			return std::nullopt;
	}
}


void StepEditor::OpenLeftInCodeEdit()
{
	const File *file = Selected();
	const std::optional<git_oid> commit = LeftCommit();
	if (file == nullptr || !commit.has_value() || !file->left->Exists()) {
		return;
	}
	const char *workdir = git_repository_workdir(fRepository.get());
	const CodeEdit::TextPoint caret = fDiffView->View(0)->CaretLocation();
	BMessage message(DiffKit::kMsgCodeEditOpenGitFile);
	message.AddString("repository", workdir != nullptr ? workdir : git_repository_path(fRepository.get()));
	message.AddString("commit", OidString(*commit).c_str());
	message.AddString("path", file->path.c_str());
	message.AddInt32("line", static_cast<int32>(caret.line + 1));
	message.AddInt32("column", static_cast<int32>(caret.column + 1));
	DiffKit::SendToCodeEdit(message);
}


// The left pane's version replaces the right one.
void StepEditor::TakeLeft()
{
	File *file = Selected();
	if (file == nullptr) {
		return;
	}
	const int leftIndex = fInput.mode == StepEditMode::Edit ? 0
		: fInput.mode == StepEditMode::Split ? 1 : file->leftVersion;
	const std::optional<FileVersion> &version = file->versions[leftIndex];
	file->deleted = !version.has_value();
	if (file->wholeFile) {
		file->taken = version;
	} else if (version.has_value()) {
		const std::string text = Text(*file->left);
		CodeEdit::TextModel &model = *file->right->Model();
		CodeEdit::ModelLock lock(model);
		model.SetText(text);
	}
	ShowSelected();
	UpdateFileList();
}


void StepEditor::Finish()
{
	if (fInput.mode == StepEditMode::Resolve) {
		std::string marked;
		for (const std::unique_ptr<File> &file : fFiles) {
			if (!file->deleted && !file->wholeFile && HasConflictMarkers(Text(*file->right))) {
				marked += "\n" + file->path;
			}
		}
		if (!marked.empty()) {
			BAlert *alert = new BAlert("Resolve", ("Conflict markers remain in:" + marked + "\n\nUse the files anyway?").c_str(),
				"Cancel", "Use them", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			DiffKit::Ask(this, alert, [this](int32 button) {
				if (button == 1 && SendResult()) {
					Quit();
				}
			});
			return;
		}
	}
	if (SendResult()) {
		Quit();
	}
}


bool StepEditor::SendResult()
{
	BMessage message(kMsgStepEdited);
	message.AddInt32("step", static_cast<int32>(fInput.step));
	message.AddInt32("mode", static_cast<int32>(fInput.mode));
	message.AddUInt64("revision", fInput.revision);
	try {
		for (const std::unique_ptr<File> &file : fFiles) {
			const std::string text = Text(*file->right);
			const bool modified = file->right->IsModified();
			if (fInput.mode == StepEditMode::Edit && !modified && file->deleted == file->initiallyDeleted
				&& (!file->wholeFile || file->taken.has_value() == file->versions[1].has_value())) {
				continue;
			}
			bool deleted = file->deleted;
			git_oid blob {};
			uint32_t mode = file->mode;
			if (!deleted && file->wholeFile) {
				deleted = !file->taken.has_value();
				if (!deleted) {
					blob = file->taken->id;
					mode = file->taken->mode;
				}
			} else if (!deleted) {
				if (fInput.mode == StepEditMode::Split && !file->right->Exists() && !modified) {
					deleted = true;
				} else {
					Check(git_blob_create_from_buffer(&blob, fRepository.get(), text.data(), text.size()),
						"Cannot store the file");
				}
			}
			message.AddString("path", file->path.c_str());
			message.AddBool("deleted", deleted);
			message.AddData("blob", B_RAW_TYPE, &blob, sizeof(blob));
			message.AddInt32("fileMode", static_cast<int32>(mode == 0 ? static_cast<uint32_t>(GIT_FILEMODE_BLOB) : mode));
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
		return false;
	}
	if (fChangeView->Message() != nullptr) {
		message.AddString("message", fChangeView->MessageText().c_str());
	}
	fTarget.SendMessage(&message);
	return true;
}


void StepEditor::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case GitDiff::kMsgChangeViewFileSelected:
			ShowSelected();
			break;
		case kMsgLeftVersion:
			if (File *file = Selected()) {
				file->leftVersion = message->GetInt32("version", 0);
				file->left->Load(ReadVersion(file->versions[file->leftVersion]), true);
				ShowSelected();
				fDiffView->RefreshDiff();
			}
			break;
		case kMsgDeleteToggled:
			if (File *file = Selected()) {
				// From the check box, or the menu item, which toggles it.
				if (message->GetBool("toggle", false)) {
					fDeleted->SetValue(fDeleted->Value() == B_CONTROL_ON ? B_CONTROL_OFF : B_CONTROL_ON);
				}
				file->deleted = fDeleted->Value() == B_CONTROL_ON;
				ShowSelected();
				UpdateFileList();
			}
			break;
		case DiffKit::kMsgPaneLabelClicked:
			if (message->GetInt32("pane", 0) == 0) {
				OpenLeftInCodeEdit();
			}
			break;
		case kMsgTakeLeft:
			TakeLeft();
			break;
		case kMsgTakeChange:
			if (File *file = Selected(); file != nullptr && !file->wholeFile && !file->deleted) {
				fDiffView->CopyChanges(1, "Take change");
			}
			break;
		case kMsgAddFile: {
			// From the path menu, or else the "Other file" field.
			BTextControl *field = dynamic_cast<BTextControl*>(FindView("addPath"));
			const char *chosen = message->GetString("path", nullptr);
			const std::string path = chosen != nullptr ? chosen : field != nullptr ? field->Text() : "";
			if (path.empty()) {
				break;
			}
			size_t index = 0;
			while (index < fFiles.size() && fFiles[index]->path != path) {
				index++;
			}
			if (index == fFiles.size()) {
				try {
					AddFile(path, ReadPath(fRepository.get(), fInput.parent, path),
						ReadPath(fRepository.get(), fInput.result, path), std::nullopt);
				} catch (const std::exception &e) {
					ShowError(e.what());
					break;
				}
				UpdateFileList();
			}
			fChangeView->Select(std::to_string(index));
			if (chosen == nullptr && field != nullptr) {
				field->SetText("");
			}
			break;
		}
		case DiffKit::kMsgDiffEdited:
		case DiffKit::kMsgChangesCopied:
			UpdateFileList();
			break;
		case kMsgDialogOk:
			Finish();
			break;
		case kMsgDialogCancel:
			Quit();
			break;
		default:
			if (!fChangeView->HandleCommand(message)
				&& !CodeEdit::EditorCommands::Dispatch(message, TargetEditor())) {
				BWindow::MessageReceived(message);
			}
			break;
	}
}


CodeEditView *StepEditor::TargetEditor()
{
	if (CodeEditView *focused = CodeEdit::EditorCommands::FocusedEditor(this)) {
		return focused;
	}
	return fDiffView->FocusedView();
}


void StepEditor::MenusBeginning()
{
	CodeEdit::EditorCommands::UpdateMenu(KeyMenuBar(), TargetEditor());
	BWindow::MenusBeginning();
}


void StepEditor::WindowActivated(bool active)
{
	if (active) {
		CodeEdit::EditorCommands::SetActiveHost(this);
	}
	BWindow::WindowActivated(active);
}


} // GitTool
