#include "Plan.h"

#include <stdint.h>

#include <algorithm>
#include <set>


namespace GitTool {


static constexpr size_t kUnreachableListed = 20;
static constexpr size_t kUnreachableCounted = 10000;


static std::optional<git_oid> HeadCommit(git_repository *repository)
{
	git_oid id;
	if (git_reference_name_to_id(&id, repository, "HEAD") < 0) {
		git_error_clear();
		return std::nullopt;
	}
	return id;
}


static TreePtr CommitTree(git_repository *repository, const git_oid &commitId)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &commitId), "Cannot read the commit");
	CommitPtr commitPtr(commit);
	git_tree *tree = nullptr;
	Check(git_commit_tree(&tree, commit), "Cannot read the commit tree");
	return TreePtr(tree);
}


// Paths that differ between two commits.
static std::set<std::string> ChangedPaths(git_repository *repository, const std::optional<git_oid> &oldCommit,
	const git_oid &newCommit)
{
	TreePtr oldTree;
	if (oldCommit.has_value()) {
		oldTree = CommitTree(repository, *oldCommit);
	}
	TreePtr newTree = CommitTree(repository, newCommit);
	git_diff *diff = nullptr;
	Check(git_diff_tree_to_tree(&diff, repository, oldTree.get(), newTree.get(), nullptr), "Cannot compare the commits");
	DiffPtr diffPtr(diff);
	std::set<std::string> paths;
	const size_t count = git_diff_num_deltas(diff);
	for (size_t i = 0; i < count; i++) {
		const git_diff_delta *delta = git_diff_get_delta(diff, i);
		paths.insert(delta->old_file.path);
		paths.insert(delta->new_file.path);
	}
	return paths;
}


// The check on an opened worktree repository whose HEAD is oldCommit.
static WorktreeCheck CheckOpened(git_repository *worktreeRepository, const std::optional<git_oid> &oldCommit,
	const git_oid &newCommit)
{
	WorktreeCheck check;
	if (git_repository_state(worktreeRepository) != GIT_REPOSITORY_STATE_NONE) {
		check.state = WorktreeState::InProgress;
		return check;
	}

	git_status_options options = GIT_STATUS_OPTIONS_INIT;
	options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
		| GIT_STATUS_OPT_EXCLUDE_SUBMODULES | GIT_STATUS_OPT_SORT_CASE_SENSITIVELY;
	git_status_list *list = nullptr;
	Check(git_status_list_new(&list, worktreeRepository, &options), "Cannot read the worktree status");
	StatusListPtr listPtr(list);
	const size_t count = git_status_list_entrycount(list);
	if (count == 0) {
		return check;
	}

	std::set<std::string> changed;
	if (!oldCommit.has_value() || !git_oid_equal(&*oldCommit, &newCommit)) {
		changed = ChangedPaths(worktreeRepository, oldCommit, newCommit);
	}
	std::vector<std::string> carried;
	std::vector<std::string> overlapping;
	for (size_t i = 0; i < count; i++) {
		const git_status_entry *entry = git_status_byindex(list, i);
		std::set<std::string> paths;
		for (const git_diff_delta *delta : {entry->head_to_index, entry->index_to_workdir}) {
			if (delta != nullptr) {
				paths.insert(delta->old_file.path);
				paths.insert(delta->new_file.path);
			}
		}
		if (paths.empty()) {
			continue;
		}
		bool overlaps = false;
		for (const std::string &path : paths) {
			overlaps = overlaps || changed.count(path) > 0;
		}
		(overlaps ? overlapping : carried).push_back(*paths.rbegin());
	}
	if (!overlapping.empty()) {
		check.state = WorktreeState::Overlapping;
		check.files = std::move(overlapping);
	} else {
		check.state = carried.empty() ? WorktreeState::Clean : WorktreeState::Changes;
		check.files = std::move(carried);
	}
	return check;
}


WorktreeCheck CheckWorktree(
	git_repository *repository,
	const WorktreeInfo &worktree,
	const std::optional<git_oid> &oldCommit,
	const git_oid &newCommit
)
{
	WorktreeCheck check;
	if (worktree.missing) {
		check.state = WorktreeState::Missing;
		return check;
	}
	if (worktree.locked) {
		check.state = WorktreeState::Locked;
		return check;
	}
	RepositoryPtr opened = OpenWorktree(repository, worktree);
	return CheckOpened(opened.get(), oldCommit, newCommit);
}


static void Checkout(git_repository *repository, const git_oid &commitId, unsigned strategy,
	const std::vector<std::string> &paths)
{
	git_object *commit = nullptr;
	Check(git_object_lookup(&commit, repository, &commitId, GIT_OBJECT_COMMIT), "Cannot read the commit");
	ObjectPtr commitPtr(commit);
	git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
	options.checkout_strategy = strategy;
	std::vector<char*> pathPointers;
	for (const std::string &path : paths) {
		pathPointers.push_back(const_cast<char*>(path.c_str()));
	}
	if (!paths.empty()) {
		options.checkout_strategy |= GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
		options.paths.strings = pathPointers.data();
		options.paths.count = pathPointers.size();
	}
	Check(git_checkout_tree(repository, commit, &options), "Cannot update the files");
}


void UpdateWorktreeFiles(
	git_repository *repository,
	const WorktreeInfo &worktree,
	const git_oid &newCommit,
	Resolution resolution
)
{
	if (resolution == Resolution::PositionOnly) {
		return;
	}
	if (worktree.missing || worktree.locked) {
		throw GitError("The files of " + worktree.path + " cannot be updated ("
			+ (worktree.missing ? "missing" : "protected") + ")");
	}
	RepositoryPtr opened = OpenWorktree(repository, worktree);
	git_repository *worktreeRepository = opened.get();
	const std::optional<git_oid> oldCommit = HeadCommit(worktreeRepository);
	const WorktreeCheck check = CheckOpened(worktreeRepository, oldCommit, newCommit);
	switch (check.state) {
		case WorktreeState::InProgress:
			throw GitError(worktree.path + " has an unfinished operation");
		case WorktreeState::Overlapping:
			if (resolution != Resolution::Discard) {
				throw GitError(worktree.path + " has changes that the update would overwrite");
			}
			Checkout(worktreeRepository, newCommit, GIT_CHECKOUT_FORCE, check.files);
			break;
		default:
			break;
	}
	Checkout(worktreeRepository, newCommit, GIT_CHECKOUT_SAFE, {});
}


bool Plan::NeedsResolution() const
{
	for (const WorktreeEffect &effect : worktrees) {
		if (effect.check.state == WorktreeState::Overlapping) {
			return true;
		}
	}
	return false;
}


bool Plan::FilesLocked() const
{
	for (const WorktreeEffect &effect : worktrees) {
		if (effect.check.state == WorktreeState::Missing || effect.check.state == WorktreeState::Locked) {
			return true;
		}
	}
	return false;
}


bool Plan::RunsWithoutPreview() const
{
	return blocked.empty() && !destructive && unreachableCount == 0 && warnings.empty() && !NeedsResolution()
		&& !FilesLocked();
}


static std::string FileList(const std::vector<std::string> &files)
{
	constexpr size_t kListed = 10;
	std::string text;
	for (size_t i = 0; i < files.size() && i < kListed; i++) {
		text += "\n      " + files[i];
	}
	if (files.size() > kListed) {
		text += "\n      … and " + std::to_string(files.size() - kListed) + " more";
	}
	return text;
}


std::string Plan::Describe(git_repository *repository) const
{
	(void)repository;
	std::string text = title + "\n";
	if (!blocked.empty()) {
		text += "\nCannot run: " + blocked + "\n";
	}
	if (steps.size() == 1) {
		text += "\n" + steps[0] + "\n";
	} else if (!steps.empty()) {
		text += "\nChanges:\n";
		for (const std::string &step : steps) {
			text += "  • " + step + "\n";
		}
	}
	// Worktrees whose files are simply updated are not listed.
	std::vector<const WorktreeEffect*> notable;
	for (const WorktreeEffect &effect : worktrees) {
		if (effect.check.state != WorktreeState::Clean) {
			notable.push_back(&effect);
		}
	}
	if (!notable.empty()) {
		text += "\nWorktrees:\n";
		for (const WorktreeEffect *notableEffect : notable) {
			const WorktreeEffect &effect = *notableEffect;
			text += "  • " + effect.path + ": ";
			const WorktreeCheck &check = effect.check;
			switch (check.state) {
				case WorktreeState::Clean:
					text += "clean, files are updated";
					break;
				case WorktreeState::Changes:
					text += "files are updated; " + std::to_string(check.files.size())
						+ " changed files are carried over" + FileList(check.files);
					break;
				case WorktreeState::Overlapping:
					text += "changes overlap with the update in" + FileList(check.files);
					break;
				case WorktreeState::InProgress:
					text += "unfinished operation; finish or abort it first";
					break;
				case WorktreeState::Missing:
					text += "folder is missing; only the position can move";
					break;
				case WorktreeState::Locked:
					text += "protected; only the position can move";
					break;
			}
			text += "\n";
		}
	}
	if (unreachableCount > 0) {
		text += "\nCommits no longer reachable from any reference (" + std::to_string(unreachableCount)
			+ (unreachableCount >= kUnreachableCounted ? "+" : "") + "):\n";
		for (const CommitInfo &commit : unreachable) {
			text += "    " + ShortId(commit.id) + " " + commit.summary + "\n";
		}
		if (unreachableCount > unreachable.size()) {
			text += "    …\n";
		}
	}
	if (!warnings.empty()) {
		text += "\nWarnings:\n";
		for (const std::string &warning : warnings) {
			text += "  • " + warning + "\n";
		}
	}
	return text;
}


static void HideReference(git_revwalk *walk, git_repository *repository, const char *name)
{
	git_reference *reference = nullptr;
	if (git_reference_lookup(&reference, repository, name) < 0) {
		git_error_clear();
		return;
	}
	ReferencePtr referencePtr(reference);
	git_object *commit = nullptr;
	if (git_reference_peel(&commit, reference, GIT_OBJECT_COMMIT) < 0) {
		git_error_clear();
		return;
	}
	ObjectPtr commitPtr(commit);
	git_revwalk_hide(walk, git_object_id(commit));
}


std::string WorktreeHeadName(const WorktreeInfo &worktree)
{
	return worktree.IsMain() ? "HEAD" : "worktrees/" + worktree.name + "/HEAD";
}


// Commits reachable from oldIds but not from newId, the hidden commits, the
// non-tag references other than exceptReferences, or worktrees on a commit.
static std::vector<git_oid> WalkUnreachable(
	git_repository *repository,
	const std::vector<git_oid> &oldIds,
	const std::optional<git_oid> &newId,
	const std::set<std::string> &exceptReferences,
	const std::vector<git_oid> &hidden
)
{
	git_revwalk *walk = nullptr;
	Check(git_revwalk_new(&walk, repository), "Cannot read the history");
	RevwalkPtr walkPtr(walk);
	git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
	for (const git_oid &oldId : oldIds) {
		Check(git_revwalk_push(walk, &oldId), "Cannot read the history");
	}
	if (newId.has_value()) {
		git_revwalk_hide(walk, &*newId);
	}
	for (const git_oid &id : hidden) {
		git_revwalk_hide(walk, &id);
	}

	GitStrArray names;
	Check(git_reference_list(names.Get(), repository), "Cannot list the references");
	for (size_t i = 0; i < names.Count(); i++) {
		const std::string name = names.At(i);
		if (!name.starts_with("refs/tags/") && exceptReferences.count(name) == 0) {
			HideReference(walk, repository, names.At(i));
		}
	}
	// Worktrees on a commit keep it reachable.
	if (git_repository_head_detached(repository) == 1 && exceptReferences.count("HEAD") == 0) {
		HideReference(walk, repository, "HEAD");
	}
	GitStrArray worktrees;
	if (git_worktree_list(worktrees.Get(), repository) == 0) {
		for (size_t i = 0; i < worktrees.Count(); i++) {
			const std::string headName = std::string("worktrees/") + worktrees.At(i) + "/HEAD";
			std::string branch;
			std::optional<git_oid> commit;
			if (exceptReferences.count(headName) == 0 && ReadLinkedHead(repository, worktrees.At(i), branch, commit)
				&& branch.empty() && commit.has_value()) {
				git_revwalk_hide(walk, &*commit);
			}
		}
	}
	git_error_clear();

	std::vector<git_oid> commits;
	git_oid id;
	while (commits.size() < kUnreachableCounted && git_revwalk_next(&id, walk) == 0) {
		commits.push_back(id);
	}
	return commits;
}


// Tags are checked last and only those that may contain a candidate: a
// repository can have tens of thousands of tags, and hiding all of them
// makes the walk read most of the history.
void FindUnreachable(
	git_repository *repository,
	const std::vector<git_oid> &oldIds,
	const std::optional<git_oid> &newId,
	const std::set<std::string> &exceptReferences,
	Plan &plan
)
{
	std::vector<git_oid> commits = WalkUnreachable(repository, oldIds, newId, exceptReferences, {});
	if (commits.empty()) {
		return;
	}

	// A tag on a commit older than every candidate cannot contain one; the
	// margin allows for clocks that were off.
	constexpr int64_t kClockSkew = 7 * 24 * 3600;
	int64_t oldest = INT64_MAX;
	for (const git_oid &commit : commits) {
		oldest = std::min(oldest, ReadCommit(repository, commit).time);
	}
	std::vector<git_oid> tagCommits;
	git_reference_iterator *iterator = nullptr;
	Check(git_reference_iterator_glob_new(&iterator, repository, "refs/tags/*"), "Cannot list the tags");
	ReferenceIteratorPtr iteratorPtr(iterator);
	git_reference *reference = nullptr;
	while (git_reference_next(&reference, iterator) == 0) {
		ReferencePtr referencePtr(reference);
		if (exceptReferences.count(git_reference_name(reference)) > 0) {
			continue;
		}
		git_object *commit = nullptr;
		if (git_reference_peel(&commit, reference, GIT_OBJECT_COMMIT) < 0) {
			git_error_clear();
			continue;
		}
		ObjectPtr commitPtr(commit);
		if (git_commit_time(reinterpret_cast<git_commit*>(commit)) + kClockSkew >= oldest) {
			tagCommits.push_back(*git_object_id(commit));
		}
	}
	if (!tagCommits.empty()) {
		commits = WalkUnreachable(repository, oldIds, newId, exceptReferences, tagCommits);
	}

	plan.unreachableCount = commits.size();
	for (size_t i = 0; i < commits.size() && i < kUnreachableListed; i++) {
		plan.unreachable.push_back(ReadCommit(repository, commits[i]));
	}
}


static std::string MoveKind(git_repository *repository, const git_oid &oldId, const git_oid &newId)
{
	if (git_oid_equal(&oldId, &newId)) {
		return "no change";
	}
	if (git_graph_descendant_of(repository, &newId, &oldId) == 1) {
		return "fast-forward";
	}
	if (git_graph_descendant_of(repository, &oldId, &newId) == 1) {
		return "moves back";
	}
	return "not a fast-forward";
}


void PlanBranchMove(git_repository *repository, const Snapshot &snapshot, const BranchInfo &branch,
	const git_oid &newId, Plan &plan)
{
	plan.steps.push_back("Branch " + branch.name + ": " + ShortId(branch.commit) + " → " + ShortId(newId) + " ("
		+ MoveKind(repository, branch.commit, newId) + ")");
	FindUnreachable(repository, {branch.commit}, newId, {"refs/heads/" + branch.name}, plan);
	const WorktreeInfo *worktree = snapshot.WorktreeOfBranch(branch.name);
	if (worktree == nullptr || git_oid_equal(&branch.commit, &newId)) {
		return;
	}
	WorktreeEffect effect;
	effect.path = worktree->path;
	effect.check = CheckWorktree(repository, *worktree, branch.commit, newId);
	if (effect.check.state == WorktreeState::InProgress) {
		plan.blocked = worktree->path + " has an unfinished " + worktree->operation + "; finish or abort it first";
	}
	plan.worktrees.push_back(std::move(effect));
}


void RunBranchMove(
	RunContext &context,
	const std::string &branch,
	const git_oid &oldId,
	const git_oid &newId,
	const std::string &logMessage
)
{
	git_repository *repository = context.repository;
	const std::string refName = "refs/heads/" + branch;
	git_oid current;
	Check(git_reference_name_to_id(&current, repository, refName.c_str()), "Cannot read the branch");
	if (!git_oid_equal(&current, &oldId)) {
		throw GitError("Branch " + branch + " changed since the preview; refresh and try again");
	}
	if (git_oid_equal(&oldId, &newId)) {
		return;
	}
	for (const WorktreeInfo &worktree : LoadWorktrees(repository)) {
		if (worktree.branch == branch) {
			UpdateWorktreeFiles(repository, worktree, newId, context.resolution);
		}
	}
	git_reference *moved = nullptr;
	Check(git_reference_create_matching(&moved, repository, refName.c_str(), &newId, 1, &oldId, logMessage.c_str()),
		"Cannot move the branch");
	git_reference_free(moved);
}


} // GitTool
