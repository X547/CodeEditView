#include "Replay.h"


namespace GitTool {


static CommitPtr LookupCommit(git_repository *repository, const git_oid &id)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &id), "Cannot read the commit");
	return CommitPtr(commit);
}


static SignaturePtr DefaultSignature(git_repository *repository)
{
	git_signature *signature = nullptr;
	if (git_signature_default(&signature, repository) < 0) {
		throw GitError("Set user.name and user.email in the Git configuration to create commits");
	}
	return SignaturePtr(signature);
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
