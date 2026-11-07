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
#include <CodeEditView/TextModel.h>

#include "CommitDiff.h"
#include "DiffView.h"
#include "FindCommands.h"
#include "MessageEditor.h"
#include "Messages.h"


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
		BRect(140, 140, 1139, 799),
		"",
		B_TITLED_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fTarget(history),
	fRepository(OpenRepository(repositoryPath)),
	fInput(input)
{
	AddToSubset(history);
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

	fFileList = new BListView("files", B_SINGLE_SELECTION_LIST);
	fFileList->SetSelectionMessage(new BMessage(kMsgStepFileSelected));
	fDiffView = new DiffKit::DiffView();
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

	BSplitView *split = new BSplitView(B_HORIZONTAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Split<>(split)
		.Add(new BScrollView("fileScroll", fFileList, 0, false, true), 1)
		.Add(fDiffView, 4);

	BGroupLayout *fileRow = BLayoutBuilder::Group<>(B_HORIZONTAL, B_USE_SMALL_SPACING)
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
	BMenu *searchMenu = nullptr;
	BMessage *toggleDeleted = new BMessage(kMsgDeleteToggled);
	toggleDeleted->AddBool("toggle", true);
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem(input.mode == StepEditMode::Split ? "Split" : "Apply to the commit", kMsgDialogOk, B_ENTER)
			.AddItem("Close without changes", kMsgDialogCancel, 'W')
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
		.AddMenu("Search")
			.GetMenu(searchMenu)
		.End()
		.AddMenu("Changes")
			.AddItem("Take change or selected lines from the left", kMsgTakeChange, 'T')
			.AddItem("Use the left version of the file", kMsgTakeLeft)
			.AddItem("Delete the file", toggleDeleted)
			.AddSeparator()
			.AddItem("Next change", kMsgNextChange, B_DOWN_ARROW, B_SHIFT_KEY)
			.AddItem("Previous change", kMsgPreviousChange, B_UP_ARROW, B_SHIFT_KEY)
		.End()
		.AddMenu("View")
			.AddItem("Synchronize scrolling", kMsgToggleSyncScrolling)
				.GetItem(fSyncScrollingItem)
			.AddItem("Hide unchanged lines", kMsgToggleHideUnchanged)
				.GetItem(fHideUnchangedItem)
			.AddItem("Word wrap", kMsgToggleWordWrap)
				.GetItem(fWordWrapItem)
		.End();
	FindKit::AddFindItems(searchMenu);
	fSyncScrollingItem->SetMarked(true);
	fHideUnchangedItem->SetMarked(true);

	BGroupView *content = new BGroupView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(content);
	BLayoutBuilder::Group<> layout = BLayoutBuilder::Group<>(content)
		.SetInsets(B_USE_SMALL_SPACING)
		.Add(hint)
		.Add(split, 1)
		.Add(fileRow);
	if (input.mode == StepEditMode::Split) {
		fMessage = GitDiff::CreateMessageEditor("message");
		fMessage->SetTarget(BMessenger(this));
		std::string message = input.message;
		const size_t subjectEnd = message.find('\n');
		message.insert(subjectEnd == std::string::npos ? message.size() : subjectEnd, " (part 1)");
		GitDiff::SetMessageText(fMessage, message);
		BStringView *messageLabel = new BStringView("messageLabel", "Message of the first part:");
		messageLabel->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
		layout.Add(messageLabel);
		layout.Add(GitDiff::MessageScrollView("messageScroll", fMessage, 6));
	}
	layout.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	SetDefaultButton(ok);

	UpdateFileList();
	if (!fFiles.empty()) {
		fFileList->Select(0);
	}
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
	const int32 index = fFileList->CurrentSelection();
	return index >= 0 && static_cast<size_t>(index) < fFiles.size() ? fFiles[index].get() : nullptr;
}


void StepEditor::ShowSelected()
{
	File *file = Selected();
	if (file == nullptr) {
		fDiffView->SetModels(nullptr, nullptr, std::string());
		return;
	}
	static const char *kLeft[] = {"Parent", "The commit", ""};
	static const char *kRight[] = {"This commit (editable)", "First part (editable)", "Result (editable)"};
	const int mode = static_cast<int>(fInput.mode);
	const std::string left = fInput.mode == StepEditMode::Resolve ? kVersionNames[file->leftVersion] : kLeft[mode];
	fDiffView->SetLabel(0, (left + " — " + file->path).c_str());
	fDiffView->SetLabel(1, (std::string(kRight[mode]) + " — " + file->path
		+ (file->deleted ? " — deleted" : "")).c_str());
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


// The marks of the files; the items are updated in place, so editing does
// not change the selection.
void StepEditor::UpdateFileList()
{
	const bool rebuild = fFileList->CountItems() != static_cast<int32>(fFiles.size());
	if (rebuild) {
		for (int32 i = fFileList->CountItems() - 1; i >= 0; i--) {
			delete fFileList->RemoveItem(i);
		}
	}
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
		const std::string text = mark + file.path;
		if (rebuild) {
			fFileList->AddItem(new BStringItem(text.c_str()));
		} else {
			BStringItem *item = static_cast<BStringItem*>(fFileList->ItemAt(static_cast<int32>(i)));
			if (text != item->Text()) {
				item->SetText(text.c_str());
				fFileList->InvalidateItem(static_cast<int32>(i));
			}
		}
	}
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


bool StepEditor::Finish()
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
			if (alert->Go() != 1) {
				return false;
			}
		}
	}

	BMessage message(kMsgStepEdited);
	message.AddInt32("step", static_cast<int32>(fInput.step));
	message.AddInt32("mode", static_cast<int32>(fInput.mode));
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
	if (fMessage != nullptr) {
		message.AddString("message", GitDiff::MessageText(fMessage).c_str());
	}
	fTarget.SendMessage(&message);
	return true;
}


void StepEditor::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgStepFileSelected:
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
		case kMsgTakeLeft:
			TakeLeft();
			break;
		case kMsgTakeChange:
			if (File *file = Selected(); file != nullptr && !file->wholeFile && !file->deleted) {
				fDiffView->CopyChanges(1);
			}
			break;
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
		case B_UNDO:
		case B_REDO:
		case B_CUT:
		case B_COPY:
		case B_PASTE:
		case B_SELECT_ALL:
			if (CodeEditView *editor = TargetEditor()) {
				PostMessage(message, editor);
			}
			break;
		case kMsgAddFile: {
			BTextControl *field = dynamic_cast<BTextControl*>(FindView("addPath"));
			const std::string path = field != nullptr ? field->Text() : "";
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
			fFileList->Select(static_cast<int32>(index));
			field->SetText("");
			break;
		}
		case DiffKit::kMsgDiffEdited:
		case DiffKit::kMsgChangesCopied:
			UpdateFileList();
			break;
		case CodeEdit::kMsgContentsChanged:
			if (fMessage != nullptr) {
				GitDiff::UpdateMessageDecorations(fMessage);
			}
			break;
		case kMsgDialogOk:
			if (Finish()) {
				Quit();
			}
			break;
		case kMsgDialogCancel:
			Quit();
			break;
		default:
			if (!CodeEdit::EditorCommands::Dispatch(message, TargetEditor())) {
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
	return fDiffView->View(1);
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
