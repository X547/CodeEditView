#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Plan.h"
#include "Snapshot.h"


namespace GitTool {


// The versions of a conflicted file: zero ids for a missing side.
struct ConflictFile {
	std::string path;
	// The commit's parent (what the change was made on), the history it is
	// replayed onto, and the commit.
	git_oid ancestor {};
	git_oid ours {};
	git_oid theirs {};
	uint32_t ancestorMode {};
	uint32_t oursMode {};
	uint32_t theirsMode {};

	bool SameVersions(const ConflictFile &other) const;
};


// The content a step gives a file instead of the replayed one.
struct FileOverride {
	// None: the file is deleted.
	std::optional<git_oid> blob;
	uint32_t mode = GIT_FILEMODE_BLOB;
	// A conflict resolution: it applies only while the file conflicts with
	// these versions.
	std::optional<ConflictFile> resolves;
};


// One commit of an edited history.
struct EditStep {
	// The commit whose change the step applies.
	git_oid commit {};
	// Combined with the result of the step before it into one commit.
	bool squash {};
	// New message; none keeps the original (combined for a squash).
	std::optional<std::string> message;
	// Copied from another history.
	bool copied {};
	// Edited contents and conflict resolutions, by path. Contents are kept
	// as written: later edits of older commits do not change them.
	std::map<std::string, FileOverride> files;
	// The second part of a split: completes the change of the commit that
	// the step before it (the first part, the same commit) applied in part.
	bool completesPrevious {};
};


enum class StepState {
	// Older than every change: the commit stays as it is.
	Kept,
	// Replayed as a new commit.
	Rewritten,
	// Its changes are already there; it is left out.
	Empty,
	// The new base of a rebase has the same change; it is left out.
	AlreadyInBase,
	// The change does not apply; nothing after it is replayed.
	Conflict,
	// After a conflict.
	Pending,
};


struct StepResult {
	StepState state = StepState::Kept;
	// The commit that has the step's change in the result: the original
	// commit when kept, the new one when rewritten (shared by the steps
	// combined into it).
	git_oid commit {};
	// The commit the step's result is based on; none for an empty history.
	std::optional<git_oid> parent;
	// Conflicting paths, or why the step cannot be replayed.
	std::vector<std::string> conflicts;
	// The versions of the conflicting files.
	std::vector<ConflictFile> conflictFiles;
};


// Pending edits of a branch's history: a list of steps, oldest first, that
// starts as the branch's commits back to its newest merge (merges and what
// is older cannot be edited), or for a rebase, its commits that the new
// base does not have. Replaying them changes nothing in the repository
// except adding objects; Apply moves the branch once.
//
// The replay starts at the oldest position that differs from the original
// list (at the first one for a rebase); the commits before it keep their
// ids.
class HistoryEdit {
private:
	git_repository *fRepository;
	std::string fBranch;
	git_oid fTip {};
	// The editable commits, oldest first, and the parent of the oldest
	// (none at the root).
	std::vector<git_oid> fOriginal;
	std::optional<git_oid> fBase;
	// The new base of a rebase.
	std::optional<git_oid> fOnto;
	// Patch ids of the new base's commits that the branch does not have.
	std::set<std::string> fBasePatches;
	std::map<std::string, std::optional<std::string>> fPatchIds;
	std::vector<EditStep> fSteps;

	std::vector<StepResult> fResults;
	std::optional<git_oid> fNewTip;
	std::string fError;

public:
	// The newest limit commits of the branch at most.
	HistoryEdit(git_repository *repository, const std::string &branch, size_t limit);
	// A rebase onto a commit: the branch's commits that it does not have.
	// Throws if they are more than limit or include a merge.
	HistoryEdit(git_repository *repository, const std::string &branch, const git_oid &onto, size_t limit);

	const std::string &Branch() const {return fBranch;}
	const git_oid &Tip() const {return fTip;}
	const std::vector<EditStep> &Steps() const {return fSteps;}
	const std::vector<git_oid> &Original() const {return fOriginal;}
	// The commit the editable ones start on; none at the root.
	const std::optional<git_oid> &Base() const {return fBase;}
	// The new base for a rebase; none otherwise.
	const std::optional<git_oid> &Onto() const {return fOnto;}
	// What the replay starts on: the new base, or Base().
	const std::optional<git_oid> &Start() const {return fOnto.has_value() ? fOnto : fBase;}

	// Edits; indexes are positions in Steps(), position is where the
	// steps go (0 is the oldest position, Steps().size() the newest).
	// Each replays the history.
	void Move(const std::vector<size_t> &indexes, size_t position);
	// Throws for a merge commit.
	void Insert(const std::vector<git_oid> &commits, size_t position);
	void Remove(const std::vector<size_t> &indexes);
	void SetSquash(size_t index, bool squash);
	void SetMessage(size_t index, const std::string &message);
	// Sets or replaces overrides of a step's files.
	void SetFiles(size_t index, const std::map<std::string, FileOverride> &files);
	// Splits a step in two: the first applies the commit with firstPart for
	// the files it changes, the second completes the commit.
	void Split(size_t index, const std::map<std::string, FileOverride> &firstPart, const std::string &firstMessage);
	void Reset();

	// The oldest position whose step differs from the original list; none
	// without changes.
	std::optional<size_t> FirstChanged() const;
	bool HasChanges() const {return FirstChanged().has_value();}

	// The replay of the steps, one result per step.
	const std::vector<StepResult> &Results() const {return fResults;}
	// The new tip; none if a step conflicts or nothing would be left.
	const std::optional<git_oid> &NewTip() const {return fNewTip;}
	// Why the edits cannot be applied; empty if they can.
	std::string Problem() const;
	// The message a step's commit gets.
	std::string Message(size_t index) const;

	// Moves the branch to the new tip, with the files of its worktree.
	Plan PlanApply(const Snapshot &snapshot) const;

	void Replay();
};


} // GitTool
