#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Messenger.h>
#include <Window.h>

#include "CommitDiff.h"
#include "HistoryEdit.h"
#include "TextSide.h"


class BButton;
class BCheckBox;
class BListView;
class BMenuField;
class BStringView;
class CodeEditView;


namespace DiffKit {
class DiffView;
}


namespace GitTool {


enum class StepEditMode {
	// The commit's files, editable against its parent.
	Edit,
	// The files of the first part, taken from the commit's changes.
	Split,
	// The conflicted files, editable from the merge with conflict markers.
	Resolve,
};


struct StepEditorInput {
	StepEditMode mode = StepEditMode::Edit;
	size_t step {};
	// The step's commit in the edited history and the one it is based on
	// (Edit, Split).
	git_oid result {};
	std::optional<git_oid> parent;
	// Resolve.
	std::vector<ConflictFile> conflicts;
	std::string message;
};


// Edits the files of one step of a history edit and sends the result to
// the history window as kMsgStepEdited: "step", "mode" (int32), and per
// file "path" (string), "deleted" (bool), "blob" (raw git_oid), "fileMode"
// (int32); "message" (string) for Split. Modal to the history window.
class StepEditor: public BWindow {
private:
	struct File {
		std::string path;
		uint32_t mode = GIT_FILEMODE_BLOB;
		// Edit, Split: the parent's and the commit's version. Resolve: the
		// new base (ours), the commit (theirs) and its parent (ancestor).
		std::optional<FileVersion> versions[3];
		std::unique_ptr<DiffKit::TextSide> left;
		std::unique_ptr<DiffKit::TextSide> right;
		// The version the left pane shows (Resolve).
		int leftVersion {};
		bool deleted {};
		bool initiallyDeleted {};
		// Binary or not editable as text: only whole versions are taken.
		bool wholeFile {};
		// For a whole file: the version taken.
		std::optional<FileVersion> taken;
	};

	BMessenger fTarget;
	RepositoryPtr fRepository;
	StepEditorInput fInput;
	std::vector<std::unique_ptr<File>> fFiles;

	BListView *fFileList {};
	DiffKit::DiffView *fDiffView {};
	BMenuField *fLeftField {};
	BCheckBox *fDeleted {};
	BButton *fTakeLeft {};
	CodeEditView *fMessage {};

	std::optional<std::string> ReadVersion(const std::optional<FileVersion> &version);
	void AddFile(const std::string &path, const std::optional<FileVersion> &first,
		const std::optional<FileVersion> &second, const std::optional<FileVersion> &third);
	void LoadSides(File &file);
	File *Selected() const;
	void ShowSelected();
	void UpdateFileList();
	void TakeLeft();
	bool Finish();

public:
	StepEditor(BWindow *history, const std::string &repositoryPath, const StepEditorInput &input);

	void MessageReceived(BMessage *message) override;
};


} // GitTool
