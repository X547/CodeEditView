#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Window.h>

#include "CommitDiff.h"
#include "TextSide.h"


namespace DiffKit {
class DiffView;
}


namespace GitDiff {
class ChangeView;
}


namespace GitTool {


// The changes of one commit: its message, the changed files and the diff of
// the selected file against a parent. Read-only; has its own repository
// handle.
class CommitWindow: public BWindow {
private:
	using Sides = std::pair<std::unique_ptr<DiffKit::TextSide>, std::unique_ptr<DiffKit::TextSide>>;

	std::string fRepositoryPath;
	RepositoryPtr fRepository;
	CommitDetails fDetails;
	size_t fParent {};
	std::vector<FileChange> fChanges;
	// The loaded versions of the files shown, by index in fChanges.
	std::map<size_t, Sides> fSides;
	// Counts the reads of the changes; only the newest is shown.
	uint64 fLoadRequest {};

	GitDiff::ChangeView *fChangeView {};
	DiffKit::DiffView *fDiffView {};

	void ShowHeader();
	// Reads the changes against the parent on a thread, under a progress
	// dialog.
	void LoadChanges();
	void ShowChanges(std::vector<FileChange> changes);
	void ShowSelectedFile();
	const FileChange *SelectedChange() const;
	std::optional<std::pair<git_oid, std::string>> SelectedVersion(bool parent) const;
	void OpenInCodeEdit(bool parent);

public:
	// Throws GitError if the commit cannot be read.
	CommitWindow(const std::string &repositoryPath, const git_oid &id, window_feel feel);

	void MenusBeginning() override;
	void MessageReceived(BMessage *message) override;
};


} // GitTool
