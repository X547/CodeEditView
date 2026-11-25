#pragma once

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Git.h"


namespace GitTool {


using namespace DiffKit;


struct CommitInfo {
	git_oid id {};
	std::string summary;
	std::string author;
	// Seconds since the epoch, of the committer.
	int64_t time {};
	// More than one for a merge.
	size_t parents {};
};


// The commits a merge brings in through one of its parents after the first.
struct MergedBranch {
	// The parent and its position among the merge's parents (1 for the
	// second).
	git_oid tip {};
	size_t parent {};
	// The commits shown elsewhere: those reachable from these. The merges
	// among commits bring in their branches without them too.
	std::vector<git_oid> hidden;
	// Along the first parents from tip, newest first. The other commits the
	// parent brings in come in through the merges among them.
	std::vector<CommitInfo> commits;
	// There are more than the limit.
	bool truncated {};
};


// One checked-out folder: the main worktree or a linked one.
struct WorktreeInfo {
	// Name of a linked worktree; empty for the main worktree.
	std::string name;
	// Folder, without a trailing '/'.
	std::string path;
	bool missing {};
	bool locked {};
	std::string lockReason;
	// Short name of the branch the worktree is on; empty when on a commit.
	std::string branch;
	// None for a branch without commits.
	std::optional<git_oid> commit;
	int staged {};
	int changed {};
	int untracked {};
	int conflicts {};
	// Whether the counts above were read: reading them takes the longest, so
	// they can be read after the rest (LoadWorktreeStatus).
	bool statusLoaded {};
	// Unfinished merge, rebase, etc.; empty if none.
	std::string operation;

	bool IsMain() const {return name.empty();}
	bool HasChanges() const {return staged + changed + untracked + conflicts > 0;}
};


struct BranchInfo {
	std::string name;
	git_oid commit {};
	std::string summary;
	// Remote copy the branch tracks, as "origin/main"; empty if none.
	std::string upstream;
	// Full name of the tracked reference, as "refs/remotes/origin/main".
	std::string upstreamRef;
	// Tracking is configured but the remote copy does not exist.
	bool upstreamGone {};
	size_t ahead {};
	size_t behind {};
	// Path of the worktree that is on this branch; empty if none.
	std::string worktree;
	// Time of the last reflog entry, or of the commit.
	int64_t updated {};
};


struct RemoteCopyInfo {
	// "origin/main".
	std::string name;
	// "main".
	std::string branch;
	git_oid commit {};
	std::string summary;
};


struct RemoteInfo {
	std::string name;
	std::string url;
	// Empty if uploads use url.
	std::string pushUrl;
	// Newest reflog entry of its remote copies; 0 if unknown.
	int64_t lastDownload {};
	std::vector<RemoteCopyInfo> copies;
};


// A tag; the fields after target are read by LoadTagDetails.
struct TagInfo {
	std::string name;
	// The tag object of a tag with a message, otherwise the tagged object.
	git_oid target {};
	bool detailsLoaded {};
	// The commit the tag points to (peeled), if it points to one.
	git_oid commit {};
	bool hasCommit {};
	std::string summary;
	bool annotated {};
	std::string message;
	std::string tagger;
	std::string taggerEmail;
	int64_t time {};
};


// Everything the tool shows of a repository, read at one moment.
struct Snapshot {
	std::vector<WorktreeInfo> worktrees;
	std::vector<BranchInfo> branches;
	std::vector<RemoteInfo> remotes;
	std::vector<TagInfo> tags;

	// Without withStatus, the change counts of the worktrees are not read.
	static Snapshot Load(git_repository *repository, bool withStatus = true);

	const WorktreeInfo *FindWorktree(const std::string &path) const;
	const WorktreeInfo *WorktreeOfBranch(const std::string &branch) const;
	const BranchInfo *FindBranch(const std::string &name) const;
	const RemoteInfo *FindRemote(const std::string &name) const;
	const RemoteCopyInfo *FindRemoteCopy(const std::string &name) const;
	const TagInfo *FindTag(const std::string &name) const;
};


// Opens the repository containing path; for a linked worktree, the
// repository of the main worktree (or the bare repository).
RepositoryPtr OpenRepository(const std::string &path);
// The worktrees, as in Snapshot; with withStatus, with their change counts.
std::vector<WorktreeInfo> LoadWorktrees(git_repository *repository, bool withStatus = true);
// Reads the change counts of a worktree that is not missing. Each worktree
// takes a full scan of its files: in a large repository, a fraction of a
// second.
void LoadWorktreeStatus(WorktreeInfo &worktree);
// The same for each worktree that is not missing, several at a time; one
// whose status cannot be read becomes missing.
void LoadWorktreeStatus(std::vector<WorktreeInfo> &worktrees);
// The worktree with its change counts read if they were not.
WorktreeInfo WithStatus(const WorktreeInfo &worktree);
// The recorded position of a linked worktree, readable even when its
// folder is missing: the branch (empty if on a commit) and commit (none
// for a branch without commits). False if it cannot be read.
bool ReadLinkedHead(git_repository *repository, const std::string &name, std::string &branch,
	std::optional<git_oid> &commit);
// The repository of a worktree, to read its status and update its files.
RepositoryPtr OpenWorktree(git_repository *repository, const WorktreeInfo &worktree);

std::string ShortId(const git_oid &id);
std::string OidString(const git_oid &id);
CommitInfo ReadCommit(git_repository *repository, const git_oid &id);
// "1234567 summary".
std::string Describe(git_repository *repository, const git_oid &id);
// A commit from a branch, remote copy, tag, hash or other revision syntax.
git_oid ResolveCommit(git_repository *repository, const std::string &spec);
// Commits reachable from start, newest first.
std::vector<CommitInfo> History(git_repository *repository, const git_oid &start, size_t limit);
// Commits along the first parents from start, newest first.
std::vector<CommitInfo> FirstParentHistory(git_repository *repository, const git_oid &start, size_t limit);
// What each parent of a merge after the first brings in: the commits that
// neither the parents before it nor hidden reach (hidden: those shown
// elsewhere; empty for a merge on the first-parent line of the history).
// The branches are in the order of the parents, with at most limit commits
// each.
std::vector<MergedBranch> MergedBranches(git_repository *repository, const git_oid &merge,
	const std::vector<git_oid> &hidden, size_t limit);

// Validity of a branch or tag name ("refs/heads/" + name).
bool IsValidReferenceName(const std::string &fullName);

// Reads the commit, kind, message and tagger of a tag, once.
void LoadTagDetails(git_repository *repository, TagInfo &tag);

// The branches, remote copies ("origin/main") and tags ("tags/v1") of each
// commit, by OidString of the commit; in that order, each group in natural
// order.
std::map<std::string, std::vector<std::string>> ReferenceLabels(git_repository *repository);

// Order of names with numbers by their value: "v2" before "v10".
bool NaturalLess(const std::string &a, const std::string &b);


} // GitTool
