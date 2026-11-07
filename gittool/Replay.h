#pragma once

#include <string>
#include <vector>

#include "Snapshot.h"


namespace GitTool {


// In-memory merge: it creates commits in the database but moves no
// reference and touches no worktree. Conflicts are not resolved; they
// throw. Replays (rebases) are HistoryEdit's.


struct MergeResult {
	git_oid head {};
	bool upToDate {};
	bool fastForward {};
};


// Merges theirs into ours; a fast-forward when ours is an ancestor of
// theirs, otherwise a merge commit with message.
MergeResult Merge(git_repository *repository, const git_oid &ours, const git_oid &theirs, const std::string &message);


} // GitTool
