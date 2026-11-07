#include "Operations.h"

#include <sys/stat.h>
#include <unistd.h>

#include <set>

#include "Replay.h"


namespace GitTool {


static const char kLogPrefix[] = "gittool: ";


//#pragma mark - Helpers

static const BranchInfo &RequireBranch(const Snapshot &snapshot, const std::string &name)
{
	const BranchInfo *branch = snapshot.FindBranch(name);
	if (branch == nullptr) {
		throw GitError("There is no branch \"" + name + "\"");
	}
	return *branch;
}


static const TagInfo &RequireTag(const Snapshot &snapshot, const std::string &name)
{
	const TagInfo *tag = snapshot.FindTag(name);
	if (tag == nullptr) {
		throw GitError("There is no tag \"" + name + "\"");
	}
	return *tag;
}


static const RemoteInfo &RequireRemote(const Snapshot &snapshot, const std::string &name)
{
	const RemoteInfo *remote = snapshot.FindRemote(name);
	if (remote == nullptr) {
		throw GitError("There is no remote \"" + name + "\"");
	}
	return *remote;
}


static void CheckNewBranchName(const Snapshot &snapshot, const std::string &name)
{
	if (name.empty() || !IsValidReferenceName("refs/heads/" + name)) {
		throw GitError("\"" + name + "\" is not a valid branch name");
	}
	if (snapshot.FindBranch(name) != nullptr) {
		throw GitError("A branch \"" + name + "\" exists");
	}
}


static void CheckNewTagName(const Snapshot &snapshot, const std::string &name)
{
	if (name.empty() || !IsValidReferenceName("refs/tags/" + name)) {
		throw GitError("\"" + name + "\" is not a valid tag name");
	}
	if (snapshot.FindTag(name) != nullptr) {
		throw GitError("A tag \"" + name + "\" exists");
	}
}


static void CheckUpstream(const Snapshot &snapshot, const std::string &upstream)
{
	if (!upstream.empty() && snapshot.FindRemoteCopy(upstream) == nullptr && snapshot.FindBranch(upstream) == nullptr) {
		throw GitError("There is no remote copy \"" + upstream + "\"");
	}
}


static ObjectPtr LookupCommitObject(git_repository *repository, const git_oid &id)
{
	git_object *commit = nullptr;
	Check(git_object_lookup(&commit, repository, &id, GIT_OBJECT_COMMIT), "Cannot read the commit");
	return ObjectPtr(commit);
}


static ReferencePtr LookupBranch(git_repository *repository, const std::string &name)
{
	git_reference *reference = nullptr;
	Check(git_branch_lookup(&reference, repository, name.c_str(), GIT_BRANCH_LOCAL), "Cannot find the branch");
	return ReferencePtr(reference);
}


static void CreateBranch(git_repository *repository, const std::string &name, const git_oid &commitId,
	const std::string &upstream)
{
	ObjectPtr commit = LookupCommitObject(repository, commitId);
	git_reference *reference = nullptr;
	Check(git_branch_create(&reference, repository, name.c_str(), reinterpret_cast<git_commit*>(commit.get()), 0),
		"Cannot create the branch");
	ReferencePtr referencePtr(reference);
	if (!upstream.empty()) {
		Check(git_branch_set_upstream(reference, upstream.c_str()), "Cannot set the tracked branch");
	}
}


static std::string Position(const WorktreeInfo &worktree)
{
	if (!worktree.branch.empty()) {
		return "on branch " + worktree.branch;
	}
	if (worktree.commit.has_value()) {
		return "on commit " + ShortId(*worktree.commit) + " (no branch)";
	}
	return "on no commit";
}


static void SetHeadDetached(git_repository *repository, const WorktreeInfo &worktree, const git_oid &commit)
{
	RepositoryPtr opened = OpenWorktree(repository, worktree);
	Check(git_repository_set_head_detached(opened.get(), &commit), "Cannot set the worktree position");
}


static void SetHeadBranch(git_repository *repository, const WorktreeInfo &worktree, const std::string &branch)
{
	RepositoryPtr opened = OpenWorktree(repository, worktree);
	Check(git_repository_set_head(opened.get(), ("refs/heads/" + branch).c_str()), "Cannot set the worktree branch");
}


static std::optional<git_oid> HeadOf(git_repository *repository)
{
	git_oid id;
	if (git_reference_name_to_id(&id, repository, "HEAD") < 0) {
		git_error_clear();
		return std::nullopt;
	}
	return id;
}


static std::vector<std::string> ChangedFiles(git_repository *repository, bool untracked)
{
	git_status_options options = GIT_STATUS_OPTIONS_INIT;
	options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	options.flags = GIT_STATUS_OPT_EXCLUDE_SUBMODULES | GIT_STATUS_OPT_SORT_CASE_SENSITIVELY;
	if (untracked) {
		options.flags |= GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
	}
	git_status_list *list = nullptr;
	Check(git_status_list_new(&list, repository, &options), "Cannot read the status");
	StatusListPtr listPtr(list);
	std::vector<std::string> files;
	const size_t count = git_status_list_entrycount(list);
	for (size_t i = 0; i < count; i++) {
		const git_status_entry *entry = git_status_byindex(list, i);
		const git_diff_delta *delta = entry->index_to_workdir != nullptr ? entry->index_to_workdir : entry->head_to_index;
		if (delta != nullptr) {
			files.push_back(delta->new_file.path);
		}
	}
	return files;
}


static std::string FileLines(const std::vector<std::string> &files)
{
	constexpr size_t kListed = 15;
	std::string text;
	for (size_t i = 0; i < files.size() && i < kListed; i++) {
		text += "\n      " + files[i];
	}
	if (files.size() > kListed) {
		text += "\n      … and " + std::to_string(files.size() - kListed) + " more";
	}
	return text;
}


static size_t CountCommits(git_repository *repository, const git_oid &include, const git_oid &exclude)
{
	git_revwalk *walk = nullptr;
	Check(git_revwalk_new(&walk, repository), "Cannot read the history");
	RevwalkPtr walkPtr(walk);
	Check(git_revwalk_push(walk, &include), "Cannot read the history");
	Check(git_revwalk_hide(walk, &exclude), "Cannot read the history");
	size_t count = 0;
	git_oid id;
	while (git_revwalk_next(&id, walk) == 0) {
		count++;
	}
	return count;
}


static bool Contains(git_repository *repository, const git_oid &descendant, const git_oid &ancestor)
{
	return git_oid_equal(&descendant, &ancestor) || git_graph_descendant_of(repository, &descendant, &ancestor) == 1;
}


static SignaturePtr DefaultSignature(git_repository *repository)
{
	git_signature *signature = nullptr;
	if (git_signature_default(&signature, repository) < 0) {
		throw GitError("Set user.name and user.email in the Git configuration to create tags with a message");
	}
	return SignaturePtr(signature);
}


//#pragma mark - Worktrees

static std::string NewWorktreeName(git_repository *repository, const std::string &path)
{
	std::string base = path.substr(path.rfind('/') + 1);
	for (char &c : base) {
		const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
			|| c == '_' || c == '.';
		if (!allowed) {
			c = '-';
		}
	}
	if (base.empty() || base[0] == '.') {
		base = "worktree" + base;
	}
	const std::string commonDir = git_repository_commondir(repository);
	std::string name = base;
	for (int i = 2; access((commonDir + "worktrees/" + name).c_str(), F_OK) == 0; i++) {
		name = base + "-" + std::to_string(i);
	}
	return name;
}


static void AddWorktree(git_repository *repository, const std::string &name, const std::string &path,
	git_reference *branch)
{
	git_worktree_add_options options = GIT_WORKTREE_ADD_OPTIONS_INIT;
	options.ref = branch;
	git_worktree *worktree = nullptr;
	Check(git_worktree_add(&worktree, repository, name.c_str(), path.c_str(), &options), "Cannot create the worktree");
	git_worktree_free(worktree);
}


Plan PlanCreateWorktree(git_repository *repository, const Snapshot &snapshot, const std::string &path,
	const WorktreeStart &start)
{
	if (path.empty() || path[0] != '/') {
		throw GitError("Enter the full path of the new folder");
	}
	Plan plan;
	plan.title = "Create worktree " + path;
	struct stat info;
	if (stat(path.c_str(), &info) == 0) {
		plan.blocked = "The folder exists";
	}
	const std::string name = NewWorktreeName(repository, path);

	switch (start.kind) {
		case WorktreeStartKind::ExistingBranch: {
			const BranchInfo &branch = RequireBranch(snapshot, start.branch);
			if (const WorktreeInfo *other = snapshot.WorktreeOfBranch(branch.name)) {
				plan.blocked = "Branch " + branch.name + " is in worktree " + other->path;
			}
			plan.steps.push_back("New worktree " + path + " on branch " + branch.name + " (" + ShortId(branch.commit)
				+ ")");
			const std::string branchName = branch.name;
			plan.run = [name, path, branchName](RunContext &context) {
				ReferencePtr reference = LookupBranch(context.repository, branchName);
				AddWorktree(context.repository, name, path, reference.get());
			};
			break;
		}
		case WorktreeStartKind::NewBranch: {
			CheckNewBranchName(snapshot, start.branch);
			CheckUpstream(snapshot, start.upstream);
			const git_oid commit = ResolveCommit(repository, start.commit);
			plan.steps.push_back("Branch " + start.branch + ": new at " + Describe(repository, commit));
			if (!start.upstream.empty()) {
				plan.steps.push_back("Branch " + start.branch + " tracks " + start.upstream);
			}
			plan.steps.push_back("New worktree " + path + " on branch " + start.branch);
			const std::string branchName = start.branch;
			const std::string upstream = start.upstream;
			plan.run = [name, path, branchName, commit, upstream](RunContext &context) {
				CreateBranch(context.repository, branchName, commit, upstream);
				ReferencePtr reference = LookupBranch(context.repository, branchName);
				try {
					AddWorktree(context.repository, name, path, reference.get());
				} catch (const GitError &e) {
					git_branch_delete(reference.get());
					throw;
				}
			};
			break;
		}
		case WorktreeStartKind::Commit: {
			const git_oid commit = ResolveCommit(repository, start.commit);
			plan.steps.push_back("New worktree " + path + " on commit " + Describe(repository, commit) + " (no branch)");
			plan.run = [name, path, commit](RunContext &context) {
				// libgit2 creates worktrees on a branch; a temporary one is
				// used and deleted when the worktree is on the commit.
				std::string temporary = "gittool-new-worktree";
				git_oid existing;
				for (int i = 2; git_reference_name_to_id(&existing, context.repository, ("refs/heads/" + temporary).c_str()) == 0; i++) {
					temporary = "gittool-new-worktree-" + std::to_string(i);
				}
				git_error_clear();
				CreateBranch(context.repository, temporary, commit, std::string());
				ReferencePtr reference = LookupBranch(context.repository, temporary);
				try {
					AddWorktree(context.repository, name, path, reference.get());
					git_worktree *worktree = nullptr;
					Check(git_worktree_lookup(&worktree, context.repository, name.c_str()), "Cannot find the worktree");
					WorktreePtr worktreePtr(worktree);
					git_repository *opened = nullptr;
					Check(git_repository_open_from_worktree(&opened, worktree), "Cannot open the worktree");
					RepositoryPtr openedPtr(opened);
					Check(git_repository_set_head_detached(opened, &commit), "Cannot set the worktree position");
				} catch (const GitError &e) {
					git_branch_delete(reference.get());
					throw;
				}
				Check(git_branch_delete(reference.get()), "Cannot delete the temporary branch");
			};
			break;
		}
	}
	return plan;
}


Plan PlanForgetMissingWorktrees(git_repository *repository, const Snapshot &snapshot)
{
	Plan plan;
	plan.title = "Forget missing worktrees";
	std::vector<std::string> names;
	std::vector<git_oid> detached;
	std::set<std::string> heads;
	for (const WorktreeInfo &worktree : snapshot.worktrees) {
		if (!worktree.missing) {
			continue;
		}
		if (worktree.locked) {
			plan.warnings.push_back(worktree.path + " is protected; it is kept");
			continue;
		}
		plan.steps.push_back("Forget worktree " + worktree.name + " (" + worktree.path + ", " + Position(worktree) + ")");
		names.push_back(worktree.name);
		if (worktree.branch.empty() && worktree.commit.has_value()) {
			detached.push_back(*worktree.commit);
			heads.insert(WorktreeHeadName(worktree));
		}
	}
	if (names.empty()) {
		plan.blocked = "No worktree folder is missing";
		return plan;
	}
	if (!detached.empty()) {
		FindUnreachable(repository, detached, std::nullopt, heads, plan);
		plan.destructive = plan.unreachableCount > 0;
	}
	plan.run = [names](RunContext &context) {
		for (const std::string &name : names) {
			git_worktree *worktree = nullptr;
			Check(git_worktree_lookup(&worktree, context.repository, name.c_str()), "Cannot find the worktree");
			WorktreePtr worktreePtr(worktree);
			git_worktree_prune_options options = GIT_WORKTREE_PRUNE_OPTIONS_INIT;
			Check(git_worktree_prune(worktree, &options), "Cannot forget the worktree");
		}
	};
	return plan;
}


static void AddWorktreeEffect(git_repository *repository, const WorktreeInfo &worktree, const git_oid &newCommit,
	Plan &plan)
{
	WorktreeEffect effect;
	effect.path = worktree.path;
	effect.check = CheckWorktree(repository, worktree, worktree.commit, newCommit);
	if (effect.check.state == WorktreeState::InProgress) {
		plan.blocked = worktree.path + " has an unfinished " + worktree.operation + "; finish or abort it first";
	}
	plan.worktrees.push_back(std::move(effect));
}


Plan PlanSetWorktreeBranch(git_repository *repository, const Snapshot &snapshot, const WorktreeInfo &worktree,
	const std::string &branchName)
{
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	Plan plan;
	plan.title = "Set branch of worktree " + worktree.path + " to " + branch.name;
	if (worktree.missing) {
		plan.blocked = "The worktree folder is missing";
		return plan;
	}
	if (worktree.branch == branch.name) {
		plan.blocked = "The worktree is on branch " + branch.name;
		return plan;
	}
	std::optional<WorktreeInfo> other;
	if (const WorktreeInfo *otherWorktree = snapshot.WorktreeOfBranch(branch.name)) {
		if (otherWorktree->missing) {
			plan.blocked = "Branch " + branch.name + " is in the missing worktree " + otherWorktree->path
				+ "; forget it first";
			return plan;
		}
		other = *otherWorktree;
		plan.steps.push_back("Worktree " + other->path + ": on commit " + ShortId(branch.commit)
			+ " (no branch), files unchanged; it was on branch " + branch.name);
	}
	plan.steps.push_back("Worktree " + worktree.path + ": on branch " + branch.name + " (" + ShortId(branch.commit)
		+ "); it was " + Position(worktree));
	AddWorktreeEffect(repository, worktree, branch.commit, plan);
	if (worktree.branch.empty() && worktree.commit.has_value()) {
		FindUnreachable(repository, {*worktree.commit}, branch.commit, {WorktreeHeadName(worktree)}, plan);
	}
	const git_oid commit = branch.commit;
	plan.run = [worktree, other, branchName, commit](RunContext &context) {
		if (other.has_value()) {
			SetHeadDetached(context.repository, *other, commit);
		}
		UpdateWorktreeFiles(context.repository, worktree, commit, context.resolution);
		SetHeadBranch(context.repository, worktree, branchName);
	};
	return plan;
}


Plan PlanSetWorktreeCommit(git_repository *repository, const Snapshot &snapshot, const WorktreeInfo &worktree,
	const std::string &commitSpec)
{
	(void)snapshot;
	const git_oid commit = ResolveCommit(repository, commitSpec);
	Plan plan;
	plan.title = "Set worktree " + worktree.path + " to commit " + ShortId(commit);
	if (worktree.missing) {
		plan.blocked = "The worktree folder is missing";
		return plan;
	}
	plan.steps.push_back("Worktree " + worktree.path + ": on commit " + Describe(repository, commit)
		+ " (no branch); it was " + Position(worktree));
	AddWorktreeEffect(repository, worktree, commit, plan);
	if (worktree.branch.empty() && worktree.commit.has_value()) {
		FindUnreachable(repository, {*worktree.commit}, commit, {WorktreeHeadName(worktree)}, plan);
	}
	plan.run = [worktree, commit](RunContext &context) {
		UpdateWorktreeFiles(context.repository, worktree, commit, context.resolution);
		SetHeadDetached(context.repository, worktree, commit);
	};
	return plan;
}


Plan PlanCreateBranchHere(git_repository *repository, const Snapshot &snapshot, const WorktreeInfo &worktree,
	const std::string &name)
{
	CheckNewBranchName(snapshot, name);
	Plan plan;
	plan.title = "Create branch " + name + " in worktree " + worktree.path;
	if (worktree.missing || !worktree.commit.has_value()) {
		plan.blocked = worktree.missing ? "The worktree folder is missing" : "The worktree has no commit";
		return plan;
	}
	const git_oid commit = *worktree.commit;
	plan.steps.push_back("Branch " + name + ": new at " + Describe(repository, commit));
	plan.steps.push_back("Worktree " + worktree.path + ": on branch " + name + "; files unchanged");
	plan.run = [worktree, name, commit](RunContext &context) {
		CreateBranch(context.repository, name, commit, std::string());
		SetHeadBranch(context.repository, worktree, name);
	};
	return plan;
}


Plan PlanUnstageAll(git_repository *repository, const WorktreeInfo &worktree)
{
	(void)repository;
	Plan plan;
	plan.title = "Unstage all in worktree " + worktree.path;
	if (worktree.missing) {
		plan.blocked = "The worktree folder is missing";
		return plan;
	}
	if (worktree.staged == 0 && worktree.conflicts == 0) {
		plan.blocked = "Nothing is staged";
		return plan;
	}
	plan.steps.push_back("Worktree " + worktree.path + ": staged snapshot = "
		+ (worktree.commit.has_value() ? "commit " + ShortId(*worktree.commit) : std::string("empty")) + "; "
		+ std::to_string(worktree.staged) + " staged changes become changes in the files; files unchanged");
	plan.warnings.push_back("Staged content that differs from the file on disk is lost");
	plan.run = [worktree](RunContext &context) {
		RepositoryPtr opened = OpenWorktree(context.repository, worktree);
		const std::optional<git_oid> head = HeadOf(opened.get());
		if (!head.has_value()) {
			git_index *index = nullptr;
			Check(git_repository_index(&index, opened.get()), "Cannot open the index");
			IndexPtr indexPtr(index);
			Check(git_index_clear(index), "Cannot clear the index");
			Check(git_index_write(index), "Cannot write the index");
			return;
		}
		ObjectPtr commit = LookupCommitObject(opened.get(), *head);
		Check(git_reset(opened.get(), commit.get(), GIT_RESET_MIXED, nullptr), "Cannot unstage");
	};
	return plan;
}


Plan PlanDiscardChanges(git_repository *repository, const WorktreeInfo &worktree, bool untracked)
{
	Plan plan;
	plan.title = "Discard changes in worktree " + worktree.path;
	if (worktree.missing) {
		plan.blocked = "The worktree folder is missing";
		return plan;
	}
	if (!worktree.commit.has_value()) {
		plan.blocked = "The worktree has no commit";
		return plan;
	}
	RepositoryPtr opened = OpenWorktree(repository, worktree);
	const std::vector<std::string> files = ChangedFiles(opened.get(), untracked);
	if (files.empty()) {
		plan.blocked = "There are no changes";
		return plan;
	}
	plan.destructive = true;
	plan.steps.push_back("Worktree " + worktree.path + ": files and staged snapshot = commit "
		+ Describe(repository, *worktree.commit));
	if (untracked) {
		plan.steps.push_back("Untracked files are deleted (ignored files stay)");
	}
	plan.warnings.push_back("Changes lost in " + std::to_string(files.size()) + " files:" + FileLines(files));
	plan.run = [worktree, untracked](RunContext &context) {
		RepositoryPtr opened = OpenWorktree(context.repository, worktree);
		const std::optional<git_oid> head = HeadOf(opened.get());
		if (!head.has_value()) {
			throw GitError("The worktree has no commit");
		}
		ObjectPtr commit = LookupCommitObject(opened.get(), *head);
		Check(git_reset(opened.get(), commit.get(), GIT_RESET_HARD, nullptr), "Cannot discard the changes");
		if (untracked) {
			git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
			options.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_REMOVE_UNTRACKED;
			Check(git_checkout_head(opened.get(), &options), "Cannot delete untracked files");
		}
	};
	return plan;
}


Plan PlanRemoveWorktree(git_repository *repository, const WorktreeInfo &worktree, bool force)
{
	Plan plan;
	plan.title = "Remove worktree " + worktree.path;
	if (worktree.IsMain()) {
		plan.blocked = "The main worktree cannot be removed";
		return plan;
	}
	if (worktree.locked) {
		plan.blocked = "The worktree is protected; unprotect it first";
		return plan;
	}
	if (!worktree.missing && worktree.HasChanges()) {
		RepositoryPtr opened = OpenWorktree(repository, worktree);
		const std::vector<std::string> files = ChangedFiles(opened.get(), true);
		if (!force) {
			plan.blocked = "The worktree has changes; remove it anyway to discard them";
		}
		plan.destructive = true;
		plan.warnings.push_back("Changes lost in " + std::to_string(files.size()) + " files:" + FileLines(files));
	}
	if (!worktree.missing) {
		plan.steps.push_back("Delete folder " + worktree.path);
	}
	plan.steps.push_back("Forget worktree " + worktree.name + " (" + Position(worktree) + ")");
	if (!worktree.branch.empty()) {
		plan.steps.push_back("Branch " + worktree.branch + " stays");
	} else if (worktree.commit.has_value()) {
		FindUnreachable(repository, {*worktree.commit}, std::nullopt, {WorktreeHeadName(worktree)}, plan);
		plan.destructive = plan.destructive || plan.unreachableCount > 0;
	}
	const std::string name = worktree.name;
	const bool missing = worktree.missing;
	plan.run = [name, missing](RunContext &context) {
		git_worktree *linked = nullptr;
		Check(git_worktree_lookup(&linked, context.repository, name.c_str()), "Cannot find the worktree");
		WorktreePtr linkedPtr(linked);
		git_worktree_prune_options options = GIT_WORKTREE_PRUNE_OPTIONS_INIT;
		if (!missing) {
			options.flags = GIT_WORKTREE_PRUNE_VALID | GIT_WORKTREE_PRUNE_WORKING_TREE;
		}
		Check(git_worktree_prune(linked, &options), "Cannot remove the worktree");
	};
	return plan;
}


Plan PlanProtectWorktree(const WorktreeInfo &worktree, bool protect, const std::string &reason)
{
	Plan plan;
	plan.title = (protect ? "Protect worktree " : "Unprotect worktree ") + worktree.path;
	if (worktree.IsMain()) {
		plan.blocked = "The main worktree cannot be protected";
		return plan;
	}
	if (protect == worktree.locked) {
		plan.blocked = protect ? "The worktree is protected" : "The worktree is not protected";
		return plan;
	}
	plan.steps.push_back(protect
		? "Worktree " + worktree.path + " is protected" + (reason.empty() ? std::string() : ": " + reason)
		: "Worktree " + worktree.path + " is no longer protected");
	const std::string name = worktree.name;
	plan.run = [name, protect, reason](RunContext &context) {
		git_worktree *linked = nullptr;
		Check(git_worktree_lookup(&linked, context.repository, name.c_str()), "Cannot find the worktree");
		WorktreePtr linkedPtr(linked);
		if (protect) {
			Check(git_worktree_lock(linked, reason.empty() ? nullptr : reason.c_str()), "Cannot protect the worktree");
		} else {
			Check(git_worktree_unlock(linked), "Cannot unprotect the worktree");
		}
	};
	return plan;
}


Plan PlanAbortOperation(git_repository *repository, const WorktreeInfo &worktree)
{
	Plan plan;
	plan.title = "Abort the " + worktree.operation + " in worktree " + worktree.path;
	if (worktree.operation.empty() || worktree.missing) {
		plan.blocked = "There is no unfinished operation";
		return plan;
	}
	plan.destructive = true;
	plan.steps.push_back("Worktree " + worktree.path + ": the " + worktree.operation
		+ " is abandoned; files and staged snapshot return to the position before it");
	RepositoryPtr opened = OpenWorktree(repository, worktree);
	const std::vector<std::string> files = ChangedFiles(opened.get(), false);
	if (!files.empty()) {
		plan.warnings.push_back("Changes lost in " + std::to_string(files.size()) + " files:" + FileLines(files));
	}
	plan.run = [worktree](RunContext &context) {
		RepositoryPtr opened = OpenWorktree(context.repository, worktree);
		git_rebase *rebase = nullptr;
		if (worktree.operation == "rebase" && git_rebase_open(&rebase, opened.get(), nullptr) == 0) {
			RebasePtr rebasePtr(rebase);
			Check(git_rebase_abort(rebase), "Cannot abort the rebase");
			return;
		}
		git_error_clear();
		const std::optional<git_oid> head = HeadOf(opened.get());
		if (head.has_value()) {
			ObjectPtr commit = LookupCommitObject(opened.get(), *head);
			Check(git_reset(opened.get(), commit.get(), GIT_RESET_HARD, nullptr), "Cannot restore the files");
		}
		Check(git_repository_state_cleanup(opened.get()), "Cannot clear the operation state");
	};
	return plan;
}


//#pragma mark - Branches

Plan PlanCreateBranch(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &commitSpec, const std::string &upstream)
{
	CheckNewBranchName(snapshot, name);
	CheckUpstream(snapshot, upstream);
	const git_oid commit = ResolveCommit(repository, commitSpec);
	Plan plan;
	plan.title = "Create branch " + name;
	plan.steps.push_back("Branch " + name + ": new at " + Describe(repository, commit));
	if (!upstream.empty()) {
		plan.steps.push_back("Branch " + name + " tracks " + upstream);
	}
	plan.run = [name, commit, upstream](RunContext &context) {
		CreateBranch(context.repository, name, commit, upstream);
	};
	return plan;
}


static void SetBranchMoveRun(Plan &plan, const BranchInfo &branch, const git_oid &newId, const std::string &log)
{
	const std::string name = branch.name;
	const git_oid oldId = branch.commit;
	plan.run = [name, oldId, newId, log](RunContext &context) {
		RunBranchMove(context, name, oldId, newId, kLogPrefix + log);
	};
}


Plan PlanSetBranchCommit(git_repository *repository, const Snapshot &snapshot, const std::string &branchName,
	const std::string &commitSpec)
{
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	const git_oid commit = ResolveCommit(repository, commitSpec);
	Plan plan;
	plan.title = "Set commit of branch " + branch.name + " to " + Describe(repository, commit);
	if (git_oid_equal(&commit, &branch.commit)) {
		plan.blocked = "The branch is at this commit";
		return plan;
	}
	PlanBranchMove(repository, snapshot, branch, commit, plan);
	SetBranchMoveRun(plan, branch, commit, "set commit to " + ShortId(commit));
	return plan;
}


static Plan PlanIntegrate(git_repository *repository, const Snapshot &snapshot, const BranchInfo &branch,
	const git_oid &theirs, const std::string &theirName, UpdateMode mode, const std::string &title)
{
	Plan plan;
	plan.title = title;
	if (Contains(repository, branch.commit, theirs) && mode != UpdateMode::Replay) {
		plan.blocked = "Branch " + branch.name + " already contains " + theirName;
		return plan;
	}
	git_oid target = theirs;
	switch (mode) {
		case UpdateMode::FastForward:
			if (!Contains(repository, theirs, branch.commit)) {
				plan.blocked = "Branch " + branch.name + " has " + std::to_string(CountCommits(repository, branch.commit, theirs))
					+ " commits that " + theirName + " does not have; merge or replay instead";
				return plan;
			}
			break;
		case UpdateMode::Merge: {
			const MergeResult result = Merge(repository, branch.commit, theirs,
				"Merge " + theirName + " into " + branch.name);
			if (!result.fastForward) {
				plan.steps.push_back("New merge commit " + ShortId(result.head) + " of " + branch.name + " and "
					+ theirName);
			}
			target = result.head;
			break;
		}
		case UpdateMode::Replay: {
			const ReplayResult result = Replay(repository, branch.commit, theirs);
			if (git_oid_equal(&result.head, &branch.commit)) {
				plan.blocked = "Branch " + branch.name + " is already based on " + theirName;
				return plan;
			}
			if (!result.applied.empty()) {
				std::string step = "Replay " + std::to_string(result.applied.size()) + " commits onto " + theirName + ":";
				for (const CommitInfo &commit : result.applied) {
					step += "\n      " + ShortId(commit.id) + " " + commit.summary;
				}
				plan.steps.push_back(step);
			}
			if (!result.skipped.empty()) {
				std::string step = "Skip " + std::to_string(result.skipped.size())
					+ " commits already in " + theirName + " or empty:";
				for (const CommitInfo &commit : result.skipped) {
					step += "\n      " + ShortId(commit.id) + " " + commit.summary;
				}
				plan.steps.push_back(step);
			}
			target = result.head;
			break;
		}
	}
	PlanBranchMove(repository, snapshot, branch, target, plan);
	static const char *kLogs[] = {"fast-forward to ", "merge ", "replay onto "};
	SetBranchMoveRun(plan, branch, target, kLogs[static_cast<int>(mode)] + theirName);
	return plan;
}


Plan PlanUpdateFromTracked(git_repository *repository, const Snapshot &snapshot, const std::string &branchName,
	UpdateMode mode)
{
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	if (branch.upstream.empty()) {
		throw GitError("Branch " + branch.name + " does not track a remote branch");
	}
	if (branch.upstreamGone) {
		throw GitError("The tracked remote branch " + branch.upstream + " no longer exists");
	}
	const git_oid upstream = ResolveCommit(repository, branch.upstreamRef);
	return PlanIntegrate(repository, snapshot, branch, upstream, branch.upstream, mode,
		"Update branch " + branch.name + " from " + branch.upstream);
}


Plan PlanMergeInto(git_repository *repository, const Snapshot &snapshot, const std::string &branchName,
	const std::string &commitSpec)
{
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	const git_oid commit = ResolveCommit(repository, commitSpec);
	return PlanIntegrate(repository, snapshot, branch, commit, commitSpec, UpdateMode::Merge,
		"Merge " + commitSpec + " into branch " + branch.name);
}


Plan PlanReplayOnto(git_repository *repository, const Snapshot &snapshot, const std::string &branchName,
	const std::string &commitSpec)
{
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	const git_oid commit = ResolveCommit(repository, commitSpec);
	return PlanIntegrate(repository, snapshot, branch, commit, commitSpec, UpdateMode::Replay,
		"Replay branch " + branch.name + " onto " + commitSpec);
}


Plan PlanUpdateAllFromTracked(git_repository *repository, const Snapshot &snapshot)
{
	Plan plan;
	plan.title = "Update all branches from their tracked remote branches (fast-forward only)";
	std::vector<std::pair<BranchInfo, git_oid>> moves;
	for (const BranchInfo &branch : snapshot.branches) {
		if (branch.upstream.empty() || branch.upstreamGone || branch.behind == 0) {
			continue;
		}
		if (branch.ahead > 0) {
			plan.warnings.push_back("Skipped " + branch.name + ": " + std::to_string(branch.ahead)
				+ " commits not in " + branch.upstream);
			continue;
		}
		const git_oid upstream = ResolveCommit(repository, branch.upstreamRef);
		if (const WorktreeInfo *worktree = snapshot.WorktreeOfBranch(branch.name)) {
			const WorktreeState state = CheckWorktree(repository, *worktree, branch.commit, upstream).state;
			if (state != WorktreeState::Clean && state != WorktreeState::Changes) {
				plan.warnings.push_back("Skipped " + branch.name + ": its worktree " + worktree->path
					+ " cannot be updated");
				continue;
			}
		}
		PlanBranchMove(repository, snapshot, branch, upstream, plan);
		moves.push_back({branch, upstream});
	}
	if (moves.empty()) {
		plan.blocked = "No branch can be fast-forwarded";
		return plan;
	}
	plan.run = [moves](RunContext &context) {
		for (const auto &[branch, upstream] : moves) {
			RunBranchMove(context, branch.name, branch.commit, upstream,
				std::string(kLogPrefix) + "fast-forward to " + branch.upstream);
		}
	};
	return plan;
}


Plan PlanDeleteBranches(git_repository *repository, const Snapshot &snapshot, const std::vector<std::string> &names)
{
	Plan plan;
	plan.title = names.size() == 1 ? "Delete branch " + names[0] : "Delete " + std::to_string(names.size()) + " branches";
	std::set<std::string> except;
	std::vector<git_oid> lost;
	std::vector<std::pair<std::string, git_oid>> branches;
	std::vector<std::pair<WorktreeInfo, git_oid>> detach;
	for (const std::string &name : names) {
		const BranchInfo &branch = RequireBranch(snapshot, name);
		except.insert("refs/heads/" + name);
		branches.push_back({name, branch.commit});
		plan.steps.push_back("Delete branch " + name + " (at " + ShortId(branch.commit) + ")");
		if (const WorktreeInfo *worktree = snapshot.WorktreeOfBranch(name)) {
			if (worktree->missing) {
				plan.blocked = "Branch " + name + " is in the missing worktree " + worktree->path + "; forget it first";
			}
			plan.steps.push_back("Worktree " + worktree->path + ": on commit " + ShortId(branch.commit)
				+ " (no branch), files unchanged");
			detach.push_back({*worktree, branch.commit});
		} else {
			lost.push_back(branch.commit);
		}
	}
	if (!lost.empty()) {
		FindUnreachable(repository, lost, std::nullopt, except, plan);
	}
	if (plan.unreachableCount > 0) {
		plan.destructive = true;
		plan.warnings.push_back("A deleted branch has no previous positions: its commits cannot be restored from here");
	}
	plan.run = [branches, detach](RunContext &context) {
		for (const auto &[worktree, commit] : detach) {
			SetHeadDetached(context.repository, worktree, commit);
		}
		for (const auto &[name, commit] : branches) {
			ReferencePtr reference = LookupBranch(context.repository, name);
			const git_oid *target = git_reference_target(reference.get());
			if (target == nullptr || !git_oid_equal(target, &commit)) {
				throw GitError("Branch " + name + " changed since the preview; refresh and try again");
			}
			Check(git_branch_delete(reference.get()), "Cannot delete the branch");
		}
	};
	return plan;
}


Plan PlanRenameBranch(git_repository *repository, const Snapshot &snapshot, const std::string &branchName,
	const std::string &newName)
{
	(void)repository;
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	CheckNewBranchName(snapshot, newName);
	Plan plan;
	plan.title = "Rename branch " + branch.name + " to " + newName;
	plan.steps.push_back("Branch " + branch.name + " → " + newName);
	if (!branch.upstream.empty()) {
		plan.steps.push_back("Branch " + newName + " keeps tracking " + branch.upstream);
	}
	if (!branch.worktree.empty()) {
		plan.steps.push_back("Worktree " + branch.worktree + " stays on the branch");
	}
	plan.run = [branchName, newName](RunContext &context) {
		ReferencePtr reference = LookupBranch(context.repository, branchName);
		git_reference *renamed = nullptr;
		Check(git_branch_move(&renamed, reference.get(), newName.c_str(), 0), "Cannot rename the branch");
		git_reference_free(renamed);
	};
	return plan;
}


Plan PlanSetUpstream(git_repository *repository, const Snapshot &snapshot, const std::string &branchName,
	const std::string &upstream)
{
	(void)repository;
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	CheckUpstream(snapshot, upstream);
	Plan plan;
	if (upstream.empty()) {
		plan.title = "Stop tracking for branch " + branch.name;
		if (branch.upstream.empty()) {
			plan.blocked = "The branch does not track a remote branch";
		}
		plan.steps.push_back("Branch " + branch.name + " stops tracking " + branch.upstream);
	} else {
		plan.title = "Set tracked remote branch of " + branch.name;
		plan.steps.push_back("Branch " + branch.name + " tracks " + upstream
			+ (branch.upstream.empty() ? std::string() : " instead of " + branch.upstream));
	}
	plan.run = [branchName, upstream](RunContext &context) {
		ReferencePtr reference = LookupBranch(context.repository, branchName);
		Check(git_branch_set_upstream(reference.get(), upstream.empty() ? nullptr : upstream.c_str()),
			"Cannot set the tracked branch");
	};
	return plan;
}


Plan PlanUploadBranch(git_repository *repository, const Snapshot &snapshot, const std::string &branchName,
	const std::string &remote, UploadMode mode, bool setTracking)
{
	const BranchInfo &branch = RequireBranch(snapshot, branchName);
	RequireRemote(snapshot, remote);
	std::string destination = branch.name;
	if (branch.upstream.starts_with(remote + "/") && !branch.upstreamGone) {
		destination = branch.upstream.substr(remote.size() + 1);
	}
	const std::string copyName = remote + "/" + destination;
	const RemoteCopyInfo *copy = snapshot.FindRemoteCopy(copyName);

	Plan plan;
	plan.network = true;
	plan.title = "Upload branch " + branch.name + " to " + copyName;
	plan.steps.push_back("Server " + remote + ": branch " + destination + " "
		+ (copy != nullptr ? ShortId(copy->commit) + " → " : std::string("new at ")) + Describe(repository, branch.commit));
	plan.steps.push_back("Remote copy " + copyName + " is updated");
	std::optional<git_oid> expected;
	if (copy != nullptr) {
		expected = copy->commit;
		if (git_oid_equal(&copy->commit, &branch.commit)) {
			plan.blocked = "The server has this commit (as of the last download)";
		} else if (!Contains(repository, branch.commit, copy->commit)) {
			const size_t dropped = CountCommits(repository, copy->commit, branch.commit);
			if (mode == UploadMode::Normal) {
				plan.warnings.push_back("Not a fast-forward: the server has " + std::to_string(dropped)
					+ " commits that " + branch.name + " does not have, so it will refuse the upload");
			} else {
				plan.destructive = true;
				plan.warnings.push_back("Overwrites the server's branch: " + std::to_string(dropped)
					+ " commits are dropped from it");
			}
		}
	}
	if (mode == UploadMode::OverwriteIfUnchanged) {
		plan.steps.push_back("Only if the server's " + destination + " is still at "
			+ (expected.has_value() ? ShortId(*expected) : std::string("nothing")) + " (last download)");
	}
	if (setTracking && branch.upstream != copyName) {
		plan.steps.push_back("Branch " + branch.name + " tracks " + copyName);
	}
	const std::string refspec = std::string(mode == UploadMode::Normal ? "" : "+") + "refs/heads/" + branch.name
		+ ":refs/heads/" + destination;
	plan.run = [remote, destination, refspec, mode, expected, setTracking, branchName, copyName](RunContext &context) {
		if (mode == UploadMode::OverwriteIfUnchanged) {
			std::optional<git_oid> actual;
			for (const ServerRef &ref : ListServer(context.repository, remote, *context.network)) {
				if (ref.name == "refs/heads/" + destination) {
					actual = ref.id;
				}
			}
			const bool same = actual.has_value() == expected.has_value()
				&& (!actual.has_value() || git_oid_equal(&*actual, &*expected));
			if (!same) {
				throw GitError("The server's branch " + destination + " changed since the last download"
					+ (actual.has_value() ? " (now " + ShortId(*actual) + ")" : std::string(" (deleted)"))
					+ "; download and review first");
			}
		}
		Upload(context.repository, remote, {refspec}, *context.network);
		if (setTracking) {
			ReferencePtr reference = LookupBranch(context.repository, branchName);
			Check(git_branch_set_upstream(reference.get(), copyName.c_str()), "Cannot set the tracked branch");
		}
	};
	return plan;
}


std::vector<std::pair<std::string, std::string>> CleanupCandidates(git_repository *repository,
	const Snapshot &snapshot, const std::string &into)
{
	const BranchInfo &target = RequireBranch(snapshot, into);
	std::vector<std::pair<std::string, std::string>> candidates;
	for (const BranchInfo &branch : snapshot.branches) {
		if (branch.name == target.name || !branch.worktree.empty()) {
			continue;
		}
		if (Contains(repository, target.commit, branch.commit)) {
			candidates.push_back({branch.name, "merged into " + target.name});
		} else if (branch.upstreamGone) {
			candidates.push_back({branch.name, "tracked remote branch " + branch.upstream + " was deleted"});
		}
	}
	return candidates;
}


std::vector<ReflogEntry> PreviousPositions(git_repository *repository, const std::string &referenceName)
{
	std::vector<ReflogEntry> entries;
	git_reflog *reflog = nullptr;
	Check(git_reflog_read(&reflog, repository, referenceName.c_str()), "Cannot read the previous positions");
	ReflogPtr reflogPtr(reflog);
	const size_t count = git_reflog_entrycount(reflog);
	for (size_t i = 0; i < count; i++) {
		const git_reflog_entry *entry = git_reflog_entry_byindex(reflog, i);
		ReflogEntry result;
		result.oldId = *git_reflog_entry_id_old(entry);
		result.newId = *git_reflog_entry_id_new(entry);
		const char *message = git_reflog_entry_message(entry);
		result.message = message != nullptr ? message : "";
		if (const git_signature *committer = git_reflog_entry_committer(entry)) {
			result.time = committer->when.time;
		}
		entries.push_back(std::move(result));
	}
	return entries;
}


//#pragma mark - Remotes

static void CheckRemoteName(const Snapshot &snapshot, const std::string &name)
{
	int valid = 0;
	if (git_remote_name_is_valid(&valid, name.c_str()) < 0 || valid == 0) {
		throw GitError("\"" + name + "\" is not a valid remote name");
	}
	if (snapshot.FindRemote(name) != nullptr) {
		throw GitError("A remote \"" + name + "\" exists");
	}
}


Plan PlanAddRemote(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &url, const std::string &pushUrl)
{
	(void)repository;
	CheckRemoteName(snapshot, name);
	if (url.empty()) {
		throw GitError("Enter the download URL");
	}
	Plan plan;
	plan.title = "Add remote " + name;
	plan.steps.push_back("Remote " + name + ": download from " + url);
	if (!pushUrl.empty()) {
		plan.steps.push_back("Remote " + name + ": upload to " + pushUrl);
	}
	plan.run = [name, url, pushUrl](RunContext &context) {
		git_remote *remote = nullptr;
		Check(git_remote_create(&remote, context.repository, name.c_str(), url.c_str()), "Cannot add the remote");
		git_remote_free(remote);
		if (!pushUrl.empty()) {
			Check(git_remote_set_pushurl(context.repository, name.c_str(), pushUrl.c_str()), "Cannot set the upload URL");
		}
	};
	return plan;
}


Plan PlanEditRemote(const Snapshot &snapshot, const std::string &name, const std::string &url,
	const std::string &pushUrl)
{
	const RemoteInfo &remote = RequireRemote(snapshot, name);
	if (url.empty()) {
		throw GitError("Enter the download URL");
	}
	Plan plan;
	plan.title = "Edit the URLs of remote " + name;
	if (url != remote.url) {
		plan.steps.push_back("Download URL: " + remote.url + " → " + url);
	}
	if (pushUrl != remote.pushUrl) {
		plan.steps.push_back("Upload URL: " + (remote.pushUrl.empty() ? std::string("(download URL)") : remote.pushUrl)
			+ " → " + (pushUrl.empty() ? std::string("(download URL)") : pushUrl));
	}
	if (plan.steps.empty()) {
		plan.blocked = "Nothing changed";
	}
	plan.run = [name, url, pushUrl](RunContext &context) {
		Check(git_remote_set_url(context.repository, name.c_str(), url.c_str()), "Cannot set the download URL");
		Check(git_remote_set_pushurl(context.repository, name.c_str(), pushUrl.empty() ? nullptr : pushUrl.c_str()),
			"Cannot set the upload URL");
	};
	return plan;
}


static std::vector<std::string> BranchesTracking(const Snapshot &snapshot, const std::string &remote)
{
	std::vector<std::string> names;
	for (const BranchInfo &branch : snapshot.branches) {
		if (branch.upstreamRef.starts_with("refs/remotes/" + remote + "/")) {
			names.push_back(branch.name);
		}
	}
	return names;
}


static std::string JoinNames(const std::vector<std::string> &names)
{
	std::string text;
	for (const std::string &name : names) {
		text += (text.empty() ? "" : ", ") + name;
	}
	return text;
}


Plan PlanRenameRemote(const Snapshot &snapshot, const std::string &name, const std::string &newName)
{
	const RemoteInfo &remote = RequireRemote(snapshot, name);
	CheckRemoteName(snapshot, newName);
	Plan plan;
	plan.title = "Rename remote " + name + " to " + newName;
	plan.steps.push_back("Remote " + name + " → " + newName);
	if (!remote.copies.empty()) {
		plan.steps.push_back(std::to_string(remote.copies.size()) + " remote copies move to " + newName + "/…");
	}
	const std::vector<std::string> tracking = BranchesTracking(snapshot, name);
	if (!tracking.empty()) {
		plan.steps.push_back("Tracking follows for " + JoinNames(tracking));
	}
	plan.run = [name, newName](RunContext &context) {
		GitStrArray problems;
		Check(git_remote_rename(problems.Get(), context.repository, name.c_str(), newName.c_str()),
			"Cannot rename the remote");
	};
	return plan;
}


Plan PlanRemoveRemote(const Snapshot &snapshot, const std::string &name)
{
	const RemoteInfo &remote = RequireRemote(snapshot, name);
	Plan plan;
	plan.title = "Remove remote " + name;
	plan.steps.push_back("Remove remote " + name + " (" + remote.url + ")");
	if (!remote.copies.empty()) {
		plan.steps.push_back("Delete its " + std::to_string(remote.copies.size()) + " remote copies");
	}
	const std::vector<std::string> tracking = BranchesTracking(snapshot, name);
	if (!tracking.empty()) {
		plan.steps.push_back("Stop tracking for " + JoinNames(tracking));
	}
	plan.run = [name](RunContext &context) {
		Check(git_remote_delete(context.repository, name.c_str()), "Cannot remove the remote");
	};
	return plan;
}


Plan PlanDownload(const Snapshot &snapshot, const std::string &remoteName, bool prune, bool allTags)
{
	std::vector<std::string> remotes;
	if (remoteName.empty()) {
		for (const RemoteInfo &remote : snapshot.remotes) {
			remotes.push_back(remote.name);
		}
	} else {
		remotes.push_back(RequireRemote(snapshot, remoteName).name);
	}
	Plan plan;
	plan.network = true;
	plan.title = remoteName.empty() ? "Download from all remotes" : "Download from " + remoteName;
	if (remotes.empty()) {
		plan.blocked = "There are no remotes";
	}
	for (const std::string &remote : remotes) {
		plan.steps.push_back("Download from " + remote + (allTags ? ", with all tags" : ""));
	}
	if (prune) {
		plan.steps.push_back("Remove remote copies of branches deleted on the server");
	}
	plan.run = [remotes, prune, allTags](RunContext &context) {
		for (const std::string &remote : remotes) {
			Download(context.repository, remote, prune, allTags, *context.network);
		}
	};
	return plan;
}


Plan PlanDeleteOnServer(const Snapshot &snapshot, const std::string &remote, const std::string &fullName)
{
	RequireRemote(snapshot, remote);
	const bool isBranch = fullName.starts_with("refs/heads/");
	const std::string shortName = fullName.substr(isBranch ? 11 : fullName.starts_with("refs/tags/") ? 10 : 0);
	Plan plan;
	plan.network = true;
	plan.destructive = true;
	plan.title = std::string("Delete ") + (isBranch ? "branch " : "tag ") + shortName + " on " + remote;
	plan.steps.push_back("Server " + remote + ": delete " + (isBranch ? "branch " : "tag ") + shortName);
	if (isBranch) {
		plan.steps.push_back("Delete remote copy " + remote + "/" + shortName);
	}
	plan.run = [remote, fullName, isBranch, shortName](RunContext &context) {
		Upload(context.repository, remote, {":" + fullName}, *context.network);
		if (isBranch) {
			git_reference *copy = nullptr;
			if (git_reference_lookup(&copy, context.repository, ("refs/remotes/" + remote + "/" + shortName).c_str()) == 0) {
				ReferencePtr copyPtr(copy);
				Check(git_reference_delete(copy), "Cannot delete the remote copy");
			}
			git_error_clear();
		}
	};
	return plan;
}


std::vector<std::string> CompareWithServer(git_repository *repository, const Snapshot &snapshot,
	const std::string &remoteName, const std::vector<ServerRef> &serverRefs)
{
	const RemoteInfo &remote = RequireRemote(snapshot, remoteName);
	std::vector<std::string> lines;
	std::set<std::string> serverBranches;
	std::set<std::string> serverTags;
	for (const ServerRef &ref : serverRefs) {
		if (ref.name.starts_with("refs/heads/")) {
			const std::string name = ref.name.substr(11);
			serverBranches.insert(name);
			const RemoteCopyInfo *copy = snapshot.FindRemoteCopy(remote.name + "/" + name);
			if (copy == nullptr) {
				lines.push_back("Branch " + name + ": new on the server (" + ShortId(ref.id) + ")");
			} else if (!git_oid_equal(&copy->commit, &ref.id)) {
				lines.push_back("Branch " + name + ": changed on the server (remote copy " + ShortId(copy->commit)
					+ ", server " + ShortId(ref.id) + ")");
			}
		} else if (ref.name.starts_with("refs/tags/") && !ref.name.ends_with("^{}")) {
			const std::string name = ref.name.substr(10);
			serverTags.insert(name);
			git_oid local;
			if (git_reference_name_to_id(&local, repository, ref.name.c_str()) < 0) {
				git_error_clear();
				lines.push_back("Tag " + name + ": only on the server");
			} else if (!git_oid_equal(&local, &ref.id)) {
				lines.push_back("Tag " + name + ": differs (local " + ShortId(local) + ", server " + ShortId(ref.id) + ")");
			}
		}
	}
	for (const RemoteCopyInfo &copy : remote.copies) {
		if (copy.branch != "HEAD" && serverBranches.count(copy.branch) == 0) {
			lines.push_back("Branch " + copy.branch + ": deleted on the server (remote copy " + copy.name + " is stale)");
		}
	}
	for (const TagInfo &tag : snapshot.tags) {
		if (serverTags.count(tag.name) == 0) {
			lines.push_back("Tag " + tag.name + ": only local");
		}
	}
	return lines;
}


//#pragma mark - Tags

static void CreateTag(git_repository *repository, const std::string &name, const git_object *target,
	const std::string &message, const git_signature *tagger, bool force)
{
	git_oid id;
	if (message.empty()) {
		Check(git_tag_create_lightweight(&id, repository, name.c_str(), target, force), "Cannot create the tag");
		return;
	}
	Check(git_tag_create(&id, repository, name.c_str(), target, tagger, message.c_str(), force), "Cannot create the tag");
}


Plan PlanCreateTag(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &commitSpec, const std::string &message)
{
	CheckNewTagName(snapshot, name);
	const git_oid commit = ResolveCommit(repository, commitSpec);
	Plan plan;
	plan.title = "Create tag " + name;
	plan.steps.push_back(std::string(message.empty() ? "Simple tag " : "Tag with message ") + name + " at "
		+ Describe(repository, commit));
	plan.run = [name, commit, message](RunContext &context) {
		ObjectPtr target = LookupCommitObject(context.repository, commit);
		SignaturePtr tagger;
		if (!message.empty()) {
			tagger = DefaultSignature(context.repository);
		}
		CreateTag(context.repository, name, target.get(), message, tagger.get(), false);
	};
	return plan;
}


static const char kUploadedTagWarning[] = "Remotes that have the tag keep the old one; other people may have it too";


Plan PlanMoveTag(git_repository *repository, const Snapshot &snapshot, const std::string &name,
	const std::string &commitSpec)
{
	const TagInfo &tag = RequireTag(snapshot, name);
	const git_oid commit = ResolveCommit(repository, commitSpec);
	Plan plan;
	plan.title = "Move tag " + name;
	if (git_oid_equal(&commit, &tag.commit)) {
		plan.blocked = "The tag is at this commit";
		return plan;
	}
	plan.steps.push_back("Tag " + name + ": " + ShortId(tag.commit) + " → " + Describe(repository, commit));
	plan.warnings.push_back(kUploadedTagWarning);
	const std::string message = tag.message;
	plan.run = [name, commit, message](RunContext &context) {
		ObjectPtr target = LookupCommitObject(context.repository, commit);
		SignaturePtr tagger;
		if (!message.empty()) {
			tagger = DefaultSignature(context.repository);
		}
		CreateTag(context.repository, name, target.get(), message, tagger.get(), true);
	};
	return plan;
}


Plan PlanEditTagMessage(const Snapshot &snapshot, const std::string &name, const std::string &message)
{
	const TagInfo &tag = RequireTag(snapshot, name);
	Plan plan;
	plan.title = "Edit the message of tag " + name;
	if (message == tag.message) {
		plan.blocked = "Nothing changed";
		return plan;
	}
	plan.steps.push_back(message.empty() ? "Tag " + name + " becomes a simple tag"
		: "Tag " + name + " is recreated with the new message, at the same commit");
	plan.warnings.push_back(kUploadedTagWarning);
	const git_oid commit = tag.commit;
	plan.run = [name, commit, message](RunContext &context) {
		ObjectPtr target = LookupCommitObject(context.repository, commit);
		SignaturePtr tagger;
		if (!message.empty()) {
			tagger = DefaultSignature(context.repository);
		}
		CreateTag(context.repository, name, target.get(), message, tagger.get(), true);
	};
	return plan;
}


Plan PlanRenameTag(const Snapshot &snapshot, const std::string &name, const std::string &newName)
{
	RequireTag(snapshot, name);
	CheckNewTagName(snapshot, newName);
	Plan plan;
	plan.title = "Rename tag " + name + " to " + newName;
	plan.steps.push_back("Tag " + name + " → " + newName + " (same commit, message and tagger)");
	plan.warnings.push_back("Remotes that have the tag keep the old name");
	plan.run = [name, newName](RunContext &context) {
		git_reference *reference = nullptr;
		Check(git_reference_lookup(&reference, context.repository, ("refs/tags/" + name).c_str()), "Cannot find the tag");
		ReferencePtr referencePtr(reference);
		const git_oid *targetId = git_reference_target(reference);
		if (targetId == nullptr) {
			throw GitError("The tag is a symbolic reference");
		}
		git_tag *annotated = nullptr;
		if (git_tag_lookup(&annotated, context.repository, targetId) == 0) {
			TagPtr annotatedPtr(annotated);
			git_object *target = nullptr;
			Check(git_tag_target(&target, annotated), "Cannot read the tag target");
			ObjectPtr targetPtr(target);
			git_oid id;
			Check(git_tag_create(&id, context.repository, newName.c_str(), target, git_tag_tagger(annotated),
				git_tag_message(annotated), 0), "Cannot create the tag");
		} else {
			git_error_clear();
			git_reference *created = nullptr;
			Check(git_reference_create(&created, context.repository, ("refs/tags/" + newName).c_str(), targetId, 0,
				"rename"), "Cannot create the tag");
			git_reference_free(created);
		}
		Check(git_tag_delete(context.repository, name.c_str()), "Cannot delete the old tag");
	};
	return plan;
}


Plan PlanDeleteTag(const Snapshot &snapshot, const std::string &name)
{
	const TagInfo &tag = RequireTag(snapshot, name);
	Plan plan;
	plan.title = "Delete tag " + name;
	plan.destructive = tag.annotated;
	plan.steps.push_back("Delete tag " + name + " (at " + ShortId(tag.commit) + ")");
	if (tag.annotated) {
		plan.warnings.push_back("The tag message is lost");
	}
	plan.run = [name](RunContext &context) {
		Check(git_tag_delete(context.repository, name.c_str()), "Cannot delete the tag");
	};
	return plan;
}


Plan PlanUploadTags(const Snapshot &snapshot, const std::string &remote, const std::vector<std::string> &names)
{
	RequireRemote(snapshot, remote);
	Plan plan;
	plan.network = true;
	plan.title = "Upload tags to " + remote;
	std::vector<std::string> refspecs;
	for (const std::string &name : names) {
		const TagInfo &tag = RequireTag(snapshot, name);
		plan.steps.push_back("Server " + remote + ": tag " + name + " at " + ShortId(tag.commit));
		refspecs.push_back("refs/tags/" + name + ":refs/tags/" + name);
	}
	if (refspecs.empty()) {
		plan.blocked = "No tags selected";
	}
	plan.run = [remote, refspecs](RunContext &context) {
		Upload(context.repository, remote, refspecs, *context.network);
	};
	return plan;
}


} // GitTool
