#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Plan.h"
#include "Snapshot.h"


namespace GitTool {


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
};


enum class StepState {
	// Older than every change: the commit stays as it is.
	Kept,
	// Replayed as a new commit.
	Rewritten,
	// Its changes are already there; it is left out.
	Empty,
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
	// Conflicting paths, or why the step cannot be replayed.
	std::vector<std::string> conflicts;
};


// Pending edits of a branch's history: a list of steps, oldest first, that
// starts as the branch's commits back to its newest merge (merges and what
// is older cannot be edited). Replaying them changes nothing in the
// repository except adding objects; Apply moves the branch once.
//
// The replay starts at the oldest position that differs from the original
// list; the commits before it keep their ids.
class HistoryEdit {
private:
	git_repository *fRepository;
	std::string fBranch;
	git_oid fTip {};
	// The editable commits, oldest first, and the parent of the oldest
	// (none at the root).
	std::vector<git_oid> fOriginal;
	std::optional<git_oid> fBase;
	std::vector<EditStep> fSteps;

	std::vector<StepResult> fResults;
	std::optional<git_oid> fNewTip;
	std::string fError;

public:
	// The newest limit commits of the branch at most.
	HistoryEdit(git_repository *repository, const std::string &branch, size_t limit);

	const std::string &Branch() const {return fBranch;}
	const git_oid &Tip() const {return fTip;}
	const std::vector<EditStep> &Steps() const {return fSteps;}
	const std::vector<git_oid> &Original() const {return fOriginal;}
	// The commit the editable ones start on; none at the root.
	const std::optional<git_oid> &Base() const {return fBase;}

	// Edits; indexes are positions in Steps(), position is where the
	// steps go (0 is the oldest position, Steps().size() the newest).
	// Each replays the history.
	void Move(const std::vector<size_t> &indexes, size_t position);
	// Throws for a merge commit.
	void Insert(const std::vector<git_oid> &commits, size_t position);
	void Remove(const std::vector<size_t> &indexes);
	void SetSquash(size_t index, bool squash);
	void SetMessage(size_t index, const std::string &message);
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
