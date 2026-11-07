#include "CommitDiff.h"


namespace GitTool {


static Signature ToSignature(const git_signature *signature)
{
	Signature result;
	if (signature == nullptr) {
		return result;
	}
	result.name = signature->name != nullptr ? signature->name : "";
	result.email = signature->email != nullptr ? signature->email : "";
	result.time = signature->when.time;
	return result;
}


CommitDetails ReadCommitDetails(git_repository *repository, const git_oid &id)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &id), "Cannot read the commit");
	CommitPtr commitPtr(commit);
	CommitDetails details;
	details.id = id;
	const char *message = git_commit_message(commit);
	details.message = message != nullptr ? message : "";
	details.author = ToSignature(git_commit_author(commit));
	details.committer = ToSignature(git_commit_committer(commit));
	for (unsigned i = 0; i < git_commit_parentcount(commit); i++) {
		details.parents.push_back(*git_commit_parent_id(commit, i));
	}
	return details;
}


static char StatusLetter(git_delta_t status)
{
	switch (status) {
		case GIT_DELTA_ADDED:
			return 'A';
		case GIT_DELTA_DELETED:
			return 'D';
		case GIT_DELTA_RENAMED:
			return 'R';
		case GIT_DELTA_COPIED:
			return 'C';
		case GIT_DELTA_TYPECHANGE:
			return 'T';
		default:
			return 'M';
	}
}


std::vector<FileChange> CommitChanges(git_repository *repository, const git_oid &id, size_t parent)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &id), "Cannot read the commit");
	CommitPtr commitPtr(commit);
	git_tree *tree = nullptr;
	Check(git_commit_tree(&tree, commit), "Cannot read the commit tree");
	TreePtr treePtr(tree);
	TreePtr parentTree;
	if (parent < git_commit_parentcount(commit)) {
		git_commit *parentCommit = nullptr;
		Check(git_commit_parent(&parentCommit, commit, static_cast<unsigned>(parent)), "Cannot read the parent");
		CommitPtr parentPtr(parentCommit);
		git_tree *oldTree = nullptr;
		Check(git_commit_tree(&oldTree, parentCommit), "Cannot read the parent tree");
		parentTree.reset(oldTree);
	}

	git_diff *diff = nullptr;
	Check(git_diff_tree_to_tree(&diff, repository, parentTree.get(), tree, nullptr), "Cannot compare the commits");
	DiffPtr diffPtr(diff);
	git_diff_find_options findOptions = GIT_DIFF_FIND_OPTIONS_INIT;
	findOptions.flags = GIT_DIFF_FIND_RENAMES;
	Check(git_diff_find_similar(diff, &findOptions), "Cannot detect renames");

	std::vector<FileChange> changes;
	const size_t count = git_diff_num_deltas(diff);
	for (size_t i = 0; i < count; i++) {
		const git_diff_delta *delta = git_diff_get_delta(diff, i);
		FileChange change;
		change.status = StatusLetter(delta->status);
		if (delta->status != GIT_DELTA_ADDED) {
			change.oldPath = delta->old_file.path;
			change.oldId = delta->old_file.id;
			change.oldMode = delta->old_file.mode;
		}
		if (delta->status != GIT_DELTA_DELETED) {
			change.newPath = delta->new_file.path;
			change.newId = delta->new_file.id;
			change.newMode = delta->new_file.mode;
		}
		changes.push_back(std::move(change));
	}
	return changes;
}


std::optional<std::string> ReadFileVersion(git_repository *repository, const git_oid &id, uint32_t mode)
{
	if (git_oid_is_zero(&id)) {
		return std::nullopt;
	}
	if (mode == GIT_FILEMODE_COMMIT) {
		return "Subproject commit " + OidString(id) + "\n";
	}
	git_blob *blob = nullptr;
	Check(git_blob_lookup(&blob, repository, &id), "Cannot read the file");
	BlobPtr blobPtr(blob);
	return std::string(static_cast<const char*>(git_blob_rawcontent(blob)), git_blob_rawsize(blob));
}


} // GitTool
