#pragma once

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Snapshot.h"


namespace GitTool {


class NetworkHandler;


// How the files of a worktree relate to a change of its commit (section 2
// of the design).
enum class WorktreeState {
	Clean,
	// Changes that the commit change does not touch; they are carried over.
	Changes,
	// Changed or untracked files that the commit change would overwrite.
	Overlapping,
	// Unfinished merge, rebase, etc.
	InProgress,
	Missing,
	Locked,
};


// What to do with a worktree whose files cannot simply be updated.
enum class Resolution {
	Cancel,
	// Files and staged snapshot stay; they show as changes against the new
	// commit.
	PositionOnly,
	// The overlapping files are overwritten.
	Discard,
};


struct WorktreeCheck {
	WorktreeState state = WorktreeState::Clean;
	// Changed files carried over (Changes) or overwritten (Overlapping).
	std::vector<std::string> files;
};


// The effect of a commit change on the files of a worktree. oldCommit is
// none for a branch without commits.
WorktreeCheck CheckWorktree(
	git_repository *repository,
	const WorktreeInfo &worktree,
	const std::optional<git_oid> &oldCommit,
	const git_oid &newCommit
);

// Updates the staged snapshot and files of a worktree from its current
// commit to newCommit, as the resolution says; HEAD does not change.
// Throws if the files overlap and the resolution is Cancel.
void UpdateWorktreeFiles(
	git_repository *repository,
	const WorktreeInfo &worktree,
	const git_oid &newCommit,
	Resolution resolution
);


struct RunContext {
	git_repository *repository = nullptr;
	Resolution resolution = Resolution::Cancel;
	// Set for plans that use the network.
	NetworkHandler *network = nullptr;
};


struct WorktreeEffect {
	std::string path;
	WorktreeCheck check;
};


// An operation with its preview. Building a plan changes nothing (except
// for writing new objects to the database); run makes the changes.
struct Plan {
	std::string title;
	// One line per change: refs moved (old → new), configuration, files.
	std::vector<std::string> steps;
	std::vector<WorktreeEffect> worktrees;
	// Commits that no reference reaches after the change.
	std::vector<CommitInfo> unreachable;
	size_t unreachableCount {};
	std::vector<std::string> warnings;
	// Why the operation cannot run; empty if it can.
	std::string blocked;
	// Deletes data that cannot be recovered through previous positions.
	bool destructive {};
	// Runs in the background with a NetworkHandler.
	bool network {};
	std::function<void(RunContext &context)> run;

	// A worktree has overlapping files, so the user picks a resolution.
	bool NeedsResolution() const;
	// Only "move position only" or cancel is possible for some worktree.
	bool FilesLocked() const;
	std::string Describe(git_repository *repository) const;
};


// Adds to the plan the commits reachable from oldIds but not from newId or
// any reference other than exceptReferences. These are full reference
// names; "HEAD" stands for the position of the main worktree, and
// "worktrees/<name>/HEAD" for that of a linked worktree, when on a commit.
void FindUnreachable(
	git_repository *repository,
	const std::vector<git_oid> &oldIds,
	const std::optional<git_oid> &newId,
	const std::set<std::string> &exceptReferences,
	Plan &plan
);

// The name FindUnreachable uses for the position of a worktree.
std::string WorktreeHeadName(const WorktreeInfo &worktree);

// Adds a branch move to a plan: the step, lost commits and the effect on
// the worktree that has the branch.
void PlanBranchMove(git_repository *repository, const Snapshot &snapshot, const BranchInfo &branch,
	const git_oid &newId, Plan &plan);

// Moves a branch, failing if it no longer points to oldId, and updates the
// worktree that has it.
void RunBranchMove(
	RunContext &context,
	const std::string &branch,
	const git_oid &oldId,
	const git_oid &newId,
	const std::string &logMessage
);


} // GitTool
