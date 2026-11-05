#pragma once

#include <stdint.h>

#include <optional>
#include <string>

#include <Referenceable.h>

#include <CodeEditView/TextModel.h>

#include "TextSide.h"


namespace GitDiff {


using namespace DiffKit;


class Repository;


enum class Side {
	Head,
	Index,
	Worktree,
};


enum class DiffKind {
	// Index (left) against the working tree (right).
	Unstaged,
	// HEAD (left) against the index (right).
	Staged,
};


// The side shown in a pane (0 left, 1 right) of a diff.
inline Side SideOf(DiffKind kind, int pane)
{
	if (kind == DiffKind::Unstaged) {
		return pane == 0 ? Side::Index : Side::Worktree;
	}
	return pane == 0 ? Side::Head : Side::Index;
}


// The HEAD, index and working tree versions of one file as text models.
// The index and working tree models are edited by the user and written
// back explicitly. Errors throw GitError.
class FileDocument: public BReferenceable {
private:
	struct SideState {
		DiffKit::TextSide text;
		uint32_t mode {};

		explicit SideState(const std::string &path): text(path) {}
	};

	std::string fPath;
	SideState fSides[3];
	std::string fReadOnlyReason;

	SideState &State(Side side) {return fSides[static_cast<int>(side)];}
	const SideState &State(Side side) const {return fSides[static_cast<int>(side)];}

public:
	explicit FileDocument(const std::string &path);

	const std::string &Path() const {return fPath;}
	CodeEdit::TextModel *Model(Side side) const {return State(side).text.Model();}
	bool Exists(Side side) const {return State(side).text.Exists();}
	bool IsModified(Side side) const {return State(side).text.IsModified();}
	bool ChangedOnDisk(Side side) const {return State(side).text.ChangedOnDisk();}
	// Why the file cannot be edited or staged here; empty if it can.
	const std::string &ReadOnlyReason() const {return fReadOnlyReason;}

	// Reads all versions. Models with unsaved edits keep them (and are
	// marked changed on disk) unless force is set.
	void Load(Repository &repository, bool force = false);

	// Writes the index model to the index, with the mode of the index entry,
	// or of the working tree file for a new entry.
	void WriteIndex(Repository &repository);
	void SaveWorktree(Repository &repository);

	// The index takes the working tree version (removing a deleted file), or
	// the HEAD version (removing a file not in HEAD), and is written.
	void StageFile(Repository &repository);
	void UnstageFile(Repository &repository);
};


} // GitDiff
