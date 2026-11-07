#include "Replay.h"

#include <stdint.h>

#include <optional>
#include <set>


namespace GitTool {


static constexpr size_t kMaxUpstreamCommits = 2000;


static CommitPtr LookupCommit(git_repository *repository, const git_oid &id)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &id), "Cannot read the commit");
	return CommitPtr(commit);
}


static std::vector<git_oid> CommitsBetween(git_repository *repository, const git_oid &include, const git_oid &exclude,
	unsigned sorting, size_t limit)
{
	git_revwalk *walk = nullptr;
	Check(git_revwalk_new(&walk, repository), "Cannot read the history");
	RevwalkPtr walkPtr(walk);
	git_revwalk_sorting(walk, sorting);
	Check(git_revwalk_push(walk, &include), "Cannot read the history");
	Check(git_revwalk_hide(walk, &exclude), "Cannot read the history");
	std::vector<git_oid> ids;
	git_oid id;
	while (ids.size() < limit && git_revwalk_next(&id, walk) == 0) {
		ids.push_back(id);
	}
	return ids;
}


// Identifies the change of a commit independently of its parent; none for
// a merge or root commit.
static std::optional<std::string> PatchId(git_repository *repository, git_commit *commit)
{
	if (git_commit_parentcount(commit) != 1) {
		return std::nullopt;
	}
	git_commit *parent = nullptr;
	Check(git_commit_parent(&parent, commit, 0), "Cannot read the parent commit");
	CommitPtr parentPtr(parent);
	git_tree *oldTree = nullptr;
	git_tree *newTree = nullptr;
	Check(git_commit_tree(&oldTree, parent), "Cannot read the tree");
	TreePtr oldTreePtr(oldTree);
	Check(git_commit_tree(&newTree, commit), "Cannot read the tree");
	TreePtr newTreePtr(newTree);
	git_diff *diff = nullptr;
	Check(git_diff_tree_to_tree(&diff, repository, oldTree, newTree, nullptr), "Cannot compare the trees");
	DiffPtr diffPtr(diff);
	git_oid id;
	Check(git_diff_patchid(&id, diff, nullptr), "Cannot compute the patch ID");
	return OidString(id);
}


static SignaturePtr DefaultSignature(git_repository *repository)
{
	git_signature *signature = nullptr;
	if (git_signature_default(&signature, repository) < 0) {
		throw GitError("Set user.name and user.email in the Git configuration to create commits");
	}
	return SignaturePtr(signature);
}


ReplayResult Replay(git_repository *repository, const git_oid &tip, const git_oid &onto)
{
	ReplayResult result;
	if (git_oid_equal(&tip, &onto) || git_graph_descendant_of(repository, &tip, &onto) == 1) {
		// Already based on onto.
		result.head = tip;
		return result;
	}
	result.head = onto;
	const std::vector<git_oid> own = CommitsBetween(repository, tip, onto,
		GIT_SORT_TOPOLOGICAL | GIT_SORT_REVERSE, SIZE_MAX);
	if (own.empty()) {
		return result;
	}

	std::set<std::string> upstreamPatches;
	for (const git_oid &id : CommitsBetween(repository, onto, tip, GIT_SORT_NONE, kMaxUpstreamCommits)) {
		CommitPtr commit = LookupCommit(repository, id);
		if (std::optional<std::string> patchId = PatchId(repository, commit.get())) {
			upstreamPatches.insert(*patchId);
		}
	}

	SignaturePtr committer = DefaultSignature(repository);
	git_merge_options mergeOptions = GIT_MERGE_OPTIONS_INIT;
	for (const git_oid &id : own) {
		CommitPtr commit = LookupCommit(repository, id);
		const CommitInfo info = ReadCommit(repository, id);
		if (git_commit_parentcount(commit.get()) > 1) {
			throw GitError("Commit " + ShortId(id) + " is a merge; replaying merges is not supported yet");
		}
		const std::optional<std::string> patchId = PatchId(repository, commit.get());
		if (patchId.has_value() && upstreamPatches.count(*patchId) > 0) {
			result.skipped.push_back(info);
			continue;
		}

		CommitPtr base = LookupCommit(repository, result.head);
		git_index *index = nullptr;
		Check(git_cherrypick_commit(&index, repository, commit.get(), base.get(), 0, &mergeOptions),
			"Cannot apply the commit");
		IndexPtr indexPtr(index);
		if (git_index_has_conflicts(index)) {
			throw GitError("Commit " + ShortId(id) + " (" + info.summary + ") conflicts; resolving conflicts is not supported yet");
		}
		git_oid treeId;
		Check(git_index_write_tree_to(&treeId, index, repository), "Cannot write the tree");
		if (git_oid_equal(&treeId, git_commit_tree_id(base.get()))) {
			result.skipped.push_back(info);
			continue;
		}
		git_tree *tree = nullptr;
		Check(git_tree_lookup(&tree, repository, &treeId), "Cannot read the tree");
		TreePtr treePtr(tree);
		const git_commit *parents[] = {base.get()};
		git_oid newId;
		Check(git_commit_create(&newId, repository, nullptr, git_commit_author(commit.get()), committer.get(),
			git_commit_message_encoding(commit.get()), git_commit_message(commit.get()), tree, 1, parents),
			"Cannot create the commit");
		result.head = newId;
		result.applied.push_back(info);
	}
	return result;
}


MergeResult Merge(git_repository *repository, const git_oid &ours, const git_oid &theirs, const std::string &message)
{
	MergeResult result;
	result.head = ours;
	if (git_oid_equal(&ours, &theirs) || git_graph_descendant_of(repository, &ours, &theirs) == 1) {
		result.upToDate = true;
		return result;
	}
	if (git_graph_descendant_of(repository, &theirs, &ours) == 1) {
		result.fastForward = true;
		result.head = theirs;
		return result;
	}

	CommitPtr ourCommit = LookupCommit(repository, ours);
	CommitPtr theirCommit = LookupCommit(repository, theirs);
	git_merge_options options = GIT_MERGE_OPTIONS_INIT;
	git_index *index = nullptr;
	Check(git_merge_commits(&index, repository, ourCommit.get(), theirCommit.get(), &options), "Cannot merge");
	IndexPtr indexPtr(index);
	if (git_index_has_conflicts(index)) {
		throw GitError("The merge has conflicts; resolving conflicts is not supported yet");
	}
	git_oid treeId;
	Check(git_index_write_tree_to(&treeId, index, repository), "Cannot write the tree");
	git_tree *tree = nullptr;
	Check(git_tree_lookup(&tree, repository, &treeId), "Cannot read the tree");
	TreePtr treePtr(tree);
	SignaturePtr signature = DefaultSignature(repository);
	const git_commit *parents[] = {ourCommit.get(), theirCommit.get()};
	Check(git_commit_create(&result.head, repository, nullptr, signature.get(), signature.get(), nullptr,
		message.c_str(), tree, 2, parents), "Cannot create the merge commit");
	return result;
}


} // GitTool
