#pragma once

#include <stdint.h>

#include <optional>
#include <string>

#include <Referenceable.h>

#include <CodeEditView/TextModel.h>


namespace GitDiff {


class Repository;


enum class Side {
	Head,
	Index,
	Worktree,
};


// The HEAD, index and working tree versions of one file as text models.
// The index and working tree models are edited by the user and written
// back explicitly. Errors throw GitError.
class FileDocument: public BReferenceable {
private:
	struct SideState {
		BReference<CodeEdit::TextModel> model;
		bool exists {};
		uint32_t mode {};
		// Bytes last loaded or written; reloading skips unchanged data.
		std::optional<std::string> loaded;
		// The file changed on disk while the model had unsaved edits.
		bool changedOnDisk {};
		// Why the loaded data cannot be edited as text; empty if it can.
		std::string problem;
	};

	std::string fPath;
	SideState fSides[3];
	std::string fReadOnlyReason;

	SideState &State(Side side) {return fSides[static_cast<int>(side)];}
	const SideState &State(Side side) const {return fSides[static_cast<int>(side)];}
	void LoadSide(Side side, const std::optional<std::string> &data, bool force);
	std::string SavedData(Side side) const;

public:
	explicit FileDocument(const std::string &path);

	const std::string &Path() const {return fPath;}
	CodeEdit::TextModel *Model(Side side) const {return State(side).model.Get();}
	bool Exists(Side side) const {return State(side).exists;}
	bool IsModified(Side side) const;
	bool ChangedOnDisk(Side side) const {return State(side).changedOnDisk;}
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
