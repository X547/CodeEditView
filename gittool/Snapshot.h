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

	static Snapshot Load(git_repository *repository);

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
// The worktrees with their status, as in Snapshot.
std::vector<WorktreeInfo> LoadWorktrees(git_repository *repository);
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
