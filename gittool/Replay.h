#pragma once

#include <string>
#include <vector>

#include "Snapshot.h"


namespace GitTool {


// In-memory merge and replay (rebase): they create commits in the database
// but move no reference and touch no worktree. Conflicts are not resolved
// yet; they throw.


struct ReplayResult {
	git_oid head {};
	std::vector<CommitInfo> applied;
	// Commits whose changes are already in the base, or that become empty.
	std::vector<CommitInfo> skipped;
};


// Applies the commits reachable from tip but not from onto on top of onto,
// keeping their authors and messages.
ReplayResult Replay(git_repository *repository, const git_oid &tip, const git_oid &onto);


struct MergeResult {
	git_oid head {};
	bool upToDate {};
	bool fastForward {};
};


// Merges theirs into ours; a fast-forward when ours is an ancestor of
// theirs, otherwise a merge commit with message.
MergeResult Merge(git_repository *repository, const git_oid &ours, const git_oid &theirs, const std::string &message);


} // GitTool
