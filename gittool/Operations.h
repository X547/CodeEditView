#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Network.h"
#include "Plan.h"
#include "Snapshot.h"


namespace GitTool {


// Builders of the operations of the tool. They read the repository and the
// snapshot and return a plan; nothing changes until the plan runs.
// Invalid input throws GitError.


//#pragma mark - Worktrees

enum class WorktreeStartKind {
	ExistingBranch,
	NewBranch,
	Commit,
};


struct WorktreeStart {
	WorktreeStartKind kind = WorktreeStartKind::ExistingBranch;
	// Existing or new branch.
	std::string branch;
	// Start of a new branch, or the commit for a worktree with no branch.
	std::string commit;
	// Remote copy a new branch tracks; empty for none.
	std::string upstream;
};


Plan PlanCreateWorktree(git_repository *repository, const Snapshot &snapshot, const std::string &path,
	const WorktreeStart &start);
Plan PlanForgetMissingWorktrees(git_repository *repository, const Snapshot &snapshot);
Plan PlanSetWorktreeBranch(git_repository *repository, const Snapshot &snapshot, const WorktreeInfo &worktree,
	const std::string &branch);
Plan PlanSetWorktreeCommit(git_repository *repository, const Snapshot &snapshot, const WorktreeInfo &worktree,
	const std::string &commit);
Plan PlanCreateBranchHere(git_repository *repository, const Snapshot &snapshot, const WorktreeInfo &worktree,
	const std::string &name);
Plan PlanUnstageAll(git_repository *repository, const WorktreeInfo &worktree);
Plan PlanDiscardChanges(git_repository *repository, const WorktreeInfo &worktree, bool untracked);
Plan PlanRemoveWorktree(git_repository *repository, const WorktreeInfo &worktree, bool force);
Plan PlanProtectWorktree(const WorktreeInfo &worktree, bool protect, const std::string &reason);
Plan PlanAbortOperation(git_repository *repository, const WorktreeInfo &worktree);


//#pragma mark - Branches

enum class UpdateMode {
	FastForward,
	Merge,
	Replay,
};


enum class UploadMode {
	Normal,
	// Only if the server still has what the remote copy says.
	OverwriteIfUnchanged,
	Overwrite,
};


Plan PlanCreateBranch(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &commit, const std::string &upstream);
Plan PlanSetBranchCommit(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	const std::string &commit);
// Integrates the remote copy the branch tracks (already downloaded).
Plan PlanUpdateFromTracked(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	UpdateMode mode);
Plan PlanMergeInto(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	const std::string &commit);
Plan PlanReplayOnto(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	const std::string &commit);
// Fast-forwards every branch that is only behind its remote copy.
Plan PlanUpdateAllFromTracked(git_repository *repository, const Snapshot &snapshot);
// Deletes local branches; a worktree on one of them is set to its commit.
Plan PlanDeleteBranches(git_repository *repository, const Snapshot &snapshot, const std::vector<std::string> &names);
Plan PlanRenameBranch(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	const std::string &newName);
// An empty upstream stops tracking.
Plan PlanSetUpstream(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	const std::string &upstream);
Plan PlanUploadBranch(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	const std::string &remote, UploadMode mode, bool setTracking);
// The remote of the branch's tracked remote branch; empty for none or a
// local branch.
std::string UpstreamRemote(git_repository *repository, const std::string &branch);
// Branches without a worktree that are merged into the branch "into", or
// whose remote copy was deleted, with the reason.
std::vector<std::pair<std::string, std::string>> CleanupCandidates(git_repository *repository,
	const Snapshot &snapshot, const std::string &into);


struct ReflogEntry {
	git_oid oldId {};
	git_oid newId {};
	std::string message;
	int64_t time {};
};

// Newest first.
std::vector<ReflogEntry> PreviousPositions(git_repository *repository, const std::string &referenceName);


//#pragma mark - Remotes

Plan PlanAddRemote(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &url, const std::string &pushUrl);
Plan PlanEditRemote(const Snapshot &snapshot, const std::string &name, const std::string &url,
	const std::string &pushUrl);
Plan PlanRenameRemote(const Snapshot &snapshot, const std::string &name, const std::string &newName);
Plan PlanRemoveRemote(const Snapshot &snapshot, const std::string &name);
// From all remotes if remote is empty.
Plan PlanDownload(const Snapshot &snapshot, const std::string &remote, bool prune, bool allTags);
// fullName is a branch ("refs/heads/x") or tag on the server.
Plan PlanDeleteOnServer(const Snapshot &snapshot, const std::string &remote, const std::string &fullName);
// Differences between the server and the local copies, one line each.
std::vector<std::string> CompareWithServer(git_repository *repository, const Snapshot &snapshot,
	const std::string &remote, const std::vector<ServerRef> &serverRefs);


//#pragma mark - Gerrit

enum class ReviewState {
	// As the server decides (new changes are ready, updates keep their state).
	Default,
	WorkInProgress,
	Ready,
};


// Options of an upload for review, sent as "%" options of the target.
struct ReviewOptions {
	std::string topic;
	ReviewState state = ReviewState::Default;
	std::vector<std::string> reviewers;
	std::vector<std::string> cc;
};


// "refs/heads/<branch>:refs/for/<target>%<options>". Throws for an option
// value that cannot be sent this way.
std::string ReviewRefspec(const std::string &branch, const std::string &target, const ReviewOptions &options);
// Splits a list of names at spaces and commas.
std::vector<std::string> SplitNames(const std::string &text);
// Uploads the commits of a branch for review on target: each commit the
// remote copy of target does not have becomes a change or a new patch set.
Plan PlanUploadForReview(git_repository *repository, const Snapshot &snapshot, const std::string &branch,
	const std::string &remote, const std::string &target, const ReviewOptions &options);

// A change number, with a patch set or not: "12345", "12345/3", or a link
// to the change (".../+/12345", ".../+/12345/3", ".../#/c/12345/3").
bool ParseChange(const std::string &text, int &change, std::optional<int> &patchset);
// "refs/changes/45/12345/" for change 12345.
std::string ChangeRefPrefix(int change);
// The highest patch set of the change on the server.
std::optional<int> LatestPatchset(const std::vector<ServerRef> &serverRefs, int change);
// Where a downloaded patch set is kept: "refs/review/<remote>/<change>/<patchset>".
std::string LocalChangeRef(const std::string &remote, int change, int patchset);
// The highest downloaded patch set of the change, as a full reference name.
std::optional<std::string> LatestLocalChange(git_repository *repository, const std::string &remote, int change);
// Downloads a patch set of a change (the latest if none), and creates a
// branch at it unless branch is empty.
Plan PlanDownloadChange(const Snapshot &snapshot, const std::string &remote, int change,
	const std::optional<int> &patchset, const std::string &branch);


//#pragma mark - Tags

// An empty message creates a simple tag.
Plan PlanCreateTag(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &commit, const std::string &message);
Plan PlanMoveTag(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &commit);
// An empty message makes it a simple tag.
Plan PlanEditTagMessage(git_repository *repository, const Snapshot &snapshot,
	const std::string &name, const std::string &message);
Plan PlanRenameTag(git_repository *repository, const Snapshot &snapshot,
	const std::string &name, const std::string &newName);
Plan PlanDeleteTag(git_repository *repository, const Snapshot &snapshot,
	const std::string &name);
Plan PlanUploadTags(git_repository *repository, const Snapshot &snapshot,
	const std::string &remote, const std::vector<std::string> &names);


} // GitTool
