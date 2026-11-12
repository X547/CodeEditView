#include "Snapshot.h"

#include <stdio.h>
#include <unistd.h>

#include <algorithm>


namespace GitTool {


static const char kHeadsPrefix[] = "refs/heads/";
static const char kRemotesPrefix[] = "refs/remotes/";
static const char kTagsPrefix[] = "refs/tags/";


std::string ShortId(const git_oid &id)
{
	char text[8];
	git_oid_tostr(text, sizeof(text), &id);
	return text;
}


std::string OidString(const git_oid &id)
{
	char text[GIT_OID_MAX_HEXSIZE + 1];
	git_oid_tostr(text, sizeof(text), &id);
	return text;
}


CommitInfo ReadCommit(git_repository *repository, const git_oid &id)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &id), "Cannot read the commit");
	CommitPtr commitPtr(commit);
	CommitInfo info;
	info.id = id;
	const char *summary = git_commit_summary(commit);
	info.summary = summary != nullptr ? summary : "";
	const git_signature *author = git_commit_author(commit);
	info.author = author != nullptr && author->name != nullptr ? author->name : "";
	info.time = git_commit_time(commit);
	return info;
}


std::string Describe(git_repository *repository, const git_oid &id)
{
	try {
		return ShortId(id) + " " + ReadCommit(repository, id).summary;
	} catch (const GitError &e) {
		return ShortId(id);
	}
}


git_oid ResolveCommit(git_repository *repository, const std::string &spec)
{
	git_object *object = nullptr;
	if (spec.empty() || git_revparse_single(&object, repository, spec.c_str()) < 0) {
		throw GitError("Unknown commit: \"" + spec + "\"");
	}
	ObjectPtr objectPtr(object);
	git_object *commit = nullptr;
	if (git_object_peel(&commit, object, GIT_OBJECT_COMMIT) < 0) {
		throw GitError("Not a commit: \"" + spec + "\"");
	}
	ObjectPtr commitPtr(commit);
	return *git_object_id(commit);
}


std::vector<CommitInfo> History(git_repository *repository, const git_oid &start, size_t limit)
{
	git_revwalk *walk = nullptr;
	Check(git_revwalk_new(&walk, repository), "Cannot read the history");
	RevwalkPtr walkPtr(walk);
	git_revwalk_sorting(walk, GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
	Check(git_revwalk_push(walk, &start), "Cannot read the history");
	std::vector<CommitInfo> commits;
	git_oid id;
	while (commits.size() < limit && git_revwalk_next(&id, walk) == 0) {
		commits.push_back(ReadCommit(repository, id));
	}
	return commits;
}


bool IsValidReferenceName(const std::string &fullName)
{
	int valid = 0;
	return git_reference_name_is_valid(&valid, fullName.c_str()) == 0 && valid != 0;
}


RepositoryPtr OpenRepository(const std::string &path)
{
	git_repository *repository = nullptr;
	Check(git_repository_open_ext(&repository, path.c_str(), 0, nullptr), "Cannot open the repository");
	RepositoryPtr result(repository);
	if (git_repository_is_worktree(repository)) {
		git_repository *main = nullptr;
		Check(git_repository_open(&main, git_repository_commondir(repository)), "Cannot open the main repository");
		result.reset(main);
	}
	return result;
}


RepositoryPtr OpenWorktree(git_repository *repository, const WorktreeInfo &worktree)
{
	git_repository *result = nullptr;
	if (worktree.IsMain()) {
		Check(git_repository_open(&result, worktree.path.c_str()), "Cannot open the main worktree");
		return RepositoryPtr(result);
	}
	git_worktree *linked = nullptr;
	Check(git_worktree_lookup(&linked, repository, worktree.name.c_str()), "Cannot find the worktree");
	WorktreePtr linkedPtr(linked);
	Check(git_repository_open_from_worktree(&result, linked), "Cannot open the worktree");
	return RepositoryPtr(result);
}


bool ReadLinkedHead(git_repository *repository, const std::string &name, std::string &branch,
	std::optional<git_oid> &commit)
{
	const std::string path = std::string(git_repository_commondir(repository)) + "worktrees/" + name + "/HEAD";
	FILE *file = fopen(path.c_str(), "r");
	if (file == nullptr) {
		return false;
	}
	char line[1024] = "";
	const bool read = fgets(line, sizeof(line), file) != nullptr;
	fclose(file);
	if (!read) {
		return false;
	}
	std::string text = line;
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
		text.pop_back();
	}
	branch.clear();
	commit.reset();
	git_oid id;
	if (text.starts_with("ref: ")) {
		const std::string target = text.substr(5);
		branch = target.starts_with(kHeadsPrefix) ? target.substr(sizeof(kHeadsPrefix) - 1) : target;
		if (git_reference_name_to_id(&id, repository, target.c_str()) == 0) {
			commit = id;
		}
		git_error_clear();
		return true;
	}
	if (git_oid_fromstr(&id, text.c_str()) < 0) {
		git_error_clear();
		return false;
	}
	commit = id;
	return true;
}


static std::string StripSlash(std::string path)
{
	while (path.size() > 1 && path.back() == '/') {
		path.pop_back();
	}
	return path;
}


static const char *OperationName(int state)
{
	switch (state) {
		case GIT_REPOSITORY_STATE_NONE:
			return "";
		case GIT_REPOSITORY_STATE_MERGE:
			return "merge";
		case GIT_REPOSITORY_STATE_REVERT:
		case GIT_REPOSITORY_STATE_REVERT_SEQUENCE:
			return "revert";
		case GIT_REPOSITORY_STATE_CHERRYPICK:
		case GIT_REPOSITORY_STATE_CHERRYPICK_SEQUENCE:
			return "cherry-pick";
		case GIT_REPOSITORY_STATE_BISECT:
			return "bisect";
		case GIT_REPOSITORY_STATE_APPLY_MAILBOX:
		case GIT_REPOSITORY_STATE_APPLY_MAILBOX_OR_REBASE:
			return "patch application";
		default:
			return "rebase";
	}
}


// Position, operation and change counts of an existing worktree.
static void ReadWorktreeStatus(git_repository *repository, WorktreeInfo &worktree)
{
	RepositoryPtr opened = OpenWorktree(repository, worktree);
	git_repository *worktreeRepository = opened.get();

	git_reference *head = nullptr;
	Check(git_reference_lookup(&head, worktreeRepository, "HEAD"), "Cannot read HEAD");
	ReferencePtr headPtr(head);
	if (git_reference_type(head) == GIT_REFERENCE_SYMBOLIC) {
		const std::string target = git_reference_symbolic_target(head);
		worktree.branch = target.starts_with(kHeadsPrefix) ? target.substr(sizeof(kHeadsPrefix) - 1) : target;
	}
	git_oid commit;
	if (git_reference_name_to_id(&commit, worktreeRepository, "HEAD") == 0) {
		worktree.commit = commit;
	}
	worktree.operation = OperationName(git_repository_state(worktreeRepository));

	git_status_options options = GIT_STATUS_OPTIONS_INIT;
	options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
	git_status_list *list = nullptr;
	Check(git_status_list_new(&list, worktreeRepository, &options), "Cannot read the status");
	StatusListPtr listPtr(list);
	const size_t count = git_status_list_entrycount(list);
	for (size_t i = 0; i < count; i++) {
		const unsigned flags = git_status_byindex(list, i)->status;
		if ((flags & GIT_STATUS_CONFLICTED) != 0) {
			worktree.conflicts++;
			continue;
		}
		if ((flags & (GIT_STATUS_INDEX_NEW | GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_INDEX_DELETED
				| GIT_STATUS_INDEX_RENAMED | GIT_STATUS_INDEX_TYPECHANGE)) != 0) {
			worktree.staged++;
		}
		if ((flags & GIT_STATUS_WT_NEW) != 0) {
			worktree.untracked++;
		} else if ((flags & (GIT_STATUS_WT_MODIFIED | GIT_STATUS_WT_DELETED | GIT_STATUS_WT_RENAMED
				| GIT_STATUS_WT_TYPECHANGE)) != 0) {
			worktree.changed++;
		}
	}
}


std::vector<WorktreeInfo> LoadWorktrees(git_repository *repository)
{
	std::vector<WorktreeInfo> worktrees;
	if (const char *workdir = git_repository_workdir(repository)) {
		WorktreeInfo main;
		main.path = StripSlash(workdir);
		worktrees.push_back(std::move(main));
	}

	GitStrArray names;
	Check(git_worktree_list(names.Get(), repository), "Cannot list the worktrees");
	for (size_t i = 0; i < names.Count(); i++) {
		git_worktree *linked = nullptr;
		if (git_worktree_lookup(&linked, repository, names.At(i)) < 0) {
			continue;
		}
		WorktreePtr linkedPtr(linked);
		WorktreeInfo worktree;
		worktree.name = names.At(i);
		worktree.path = StripSlash(git_worktree_path(linked));
		worktree.missing = git_worktree_validate(linked) < 0 || access(worktree.path.c_str(), F_OK) != 0;
		git_error_clear();
		GitBuf reason;
		if (git_worktree_is_locked(reason.Get(), linked) > 0) {
			worktree.locked = true;
			worktree.lockReason = reason.String();
		}
		worktrees.push_back(std::move(worktree));
	}

	for (WorktreeInfo &worktree : worktrees) {
		if (worktree.missing) {
			// The position is still recorded in the repository.
			ReadLinkedHead(repository, worktree.name, worktree.branch, worktree.commit);
			continue;
		}
		try {
			ReadWorktreeStatus(repository, worktree);
		} catch (const GitError &e) {
			worktree.missing = true;
		}
	}
	return worktrees;
}


// Time of the newest reflog entry of a reference; 0 if there is none.
static int64_t ReflogTime(git_repository *repository, const char *name)
{
	git_reflog *reflog = nullptr;
	if (git_reflog_read(&reflog, repository, name) < 0) {
		git_error_clear();
		return 0;
	}
	ReflogPtr reflogPtr(reflog);
	if (git_reflog_entrycount(reflog) == 0) {
		return 0;
	}
	const git_signature *committer = git_reflog_entry_committer(git_reflog_entry_byindex(reflog, 0));
	return committer != nullptr ? committer->when.time : 0;
}


static std::string Summary(git_repository *repository, const git_oid &id)
{
	try {
		return ReadCommit(repository, id).summary;
	} catch (const GitError &e) {
		return std::string();
	}
}


static std::vector<BranchInfo> LoadBranches(git_repository *repository, const std::vector<WorktreeInfo> &worktrees)
{
	std::vector<BranchInfo> branches;
	git_branch_iterator *iterator = nullptr;
	Check(git_branch_iterator_new(&iterator, repository, GIT_BRANCH_LOCAL), "Cannot list the branches");
	BranchIteratorPtr iteratorPtr(iterator);
	git_reference *reference = nullptr;
	git_branch_t type;
	while (git_branch_next(&reference, &type, iterator) == 0) {
		ReferencePtr referencePtr(reference);
		git_reference *resolved = nullptr;
		if (git_reference_resolve(&resolved, reference) < 0) {
			git_error_clear();
			continue;
		}
		ReferencePtr resolvedPtr(resolved);
		BranchInfo branch;
		const char *refName = git_reference_name(reference);
		branch.name = refName + sizeof(kHeadsPrefix) - 1;
		branch.commit = *git_reference_target(resolved);
		branch.summary = Summary(repository, branch.commit);

		GitBuf upstream;
		const int result = git_branch_upstream_name(upstream.Get(), repository, refName);
		if (result == 0) {
			std::string name = upstream.String();
			branch.upstreamRef = name;
			git_oid upstreamId;
			if (git_reference_name_to_id(&upstreamId, repository, name.c_str()) == 0) {
				git_graph_ahead_behind(&branch.ahead, &branch.behind, repository, &branch.commit, &upstreamId);
			} else {
				branch.upstreamGone = true;
			}
			if (name.starts_with(kRemotesPrefix)) {
				name.erase(0, sizeof(kRemotesPrefix) - 1);
			} else if (name.starts_with(kHeadsPrefix)) {
				name.erase(0, sizeof(kHeadsPrefix) - 1);
			}
			branch.upstream = name;
		} else if (result != GIT_ENOTFOUND) {
			branch.upstream = "(unknown remote)";
			branch.upstreamGone = true;
		}
		git_error_clear();

		for (const WorktreeInfo &worktree : worktrees) {
			if (worktree.branch == branch.name) {
				branch.worktree = worktree.path;
			}
		}
		branch.updated = ReflogTime(repository, refName);
		if (branch.updated == 0) {
			try {
				branch.updated = ReadCommit(repository, branch.commit).time;
			} catch (const GitError &e) {
			}
		}
		branches.push_back(std::move(branch));
	}
	std::sort(branches.begin(), branches.end(), [](const BranchInfo &a, const BranchInfo &b) {
		return NaturalLess(a.name, b.name);
	});
	return branches;
}


static std::vector<RemoteInfo> LoadRemotes(git_repository *repository)
{
	std::vector<RemoteInfo> remotes;
	GitStrArray names;
	Check(git_remote_list(names.Get(), repository), "Cannot list the remotes");
	for (size_t i = 0; i < names.Count(); i++) {
		git_remote *remote = nullptr;
		if (git_remote_lookup(&remote, repository, names.At(i)) < 0) {
			git_error_clear();
			continue;
		}
		RemotePtr remotePtr(remote);
		RemoteInfo info;
		info.name = names.At(i);
		info.url = git_remote_url(remote) != nullptr ? git_remote_url(remote) : "";
		info.pushUrl = git_remote_pushurl(remote) != nullptr ? git_remote_pushurl(remote) : "";

		const std::string prefix = kRemotesPrefix + info.name + "/";
		git_reference_iterator *iterator = nullptr;
		Check(git_reference_iterator_glob_new(&iterator, repository, (prefix + "*").c_str()), "Cannot list the remote copies");
		ReferenceIteratorPtr iteratorPtr(iterator);
		git_reference *reference = nullptr;
		while (git_reference_next(&reference, iterator) == 0) {
			ReferencePtr referencePtr(reference);
			if (git_reference_type(reference) != GIT_REFERENCE_DIRECT) {
				continue;
			}
			RemoteCopyInfo copy;
			const char *refName = git_reference_name(reference);
			copy.name = refName + sizeof(kRemotesPrefix) - 1;
			copy.branch = refName + prefix.size();
			copy.commit = *git_reference_target(reference);
			copy.summary = Summary(repository, copy.commit);
			info.lastDownload = std::max(info.lastDownload, ReflogTime(repository, refName));
			info.copies.push_back(std::move(copy));
		}
		std::sort(info.copies.begin(), info.copies.end(), [](const RemoteCopyInfo &a, const RemoteCopyInfo &b) {
			return NaturalLess(a.name, b.name);
		});
		remotes.push_back(std::move(info));
	}
	return remotes;
}


// Names and targets only: reading the commits of thousands of tags takes
// long, so the details are read on demand.
static std::vector<TagInfo> LoadTags(git_repository *repository)
{
	std::vector<TagInfo> tags;
	git_reference_iterator *iterator = nullptr;
	Check(git_reference_iterator_glob_new(&iterator, repository, "refs/tags/*"), "Cannot list the tags");
	ReferenceIteratorPtr iteratorPtr(iterator);
	git_reference *reference = nullptr;
	while (git_reference_next(&reference, iterator) == 0) {
		ReferencePtr referencePtr(reference);
		const git_oid *target = git_reference_target(reference);
		if (target == nullptr) {
			continue;
		}
		TagInfo tag;
		tag.name = git_reference_name(reference) + sizeof(kTagsPrefix) - 1;
		tag.target = *target;
		tags.push_back(std::move(tag));
	}
	std::sort(tags.begin(), tags.end(), [](const TagInfo &a, const TagInfo &b) {
		return NaturalLess(a.name, b.name);
	});
	return tags;
}


void LoadTagDetails(git_repository *repository, TagInfo &tag)
{
	if (tag.detailsLoaded) {
		return;
	}
	tag.detailsLoaded = true;
	git_tag *annotated = nullptr;
	if (git_tag_lookup(&annotated, repository, &tag.target) == 0) {
		TagPtr annotatedPtr(annotated);
		tag.annotated = true;
		tag.message = git_tag_message(annotated) != nullptr ? git_tag_message(annotated) : "";
		if (const git_signature *tagger = git_tag_tagger(annotated)) {
			tag.tagger = tagger->name != nullptr ? tagger->name : "";
			tag.taggerEmail = tagger->email != nullptr ? tagger->email : "";
			tag.time = tagger->when.time;
		}
	}
	git_error_clear();
	git_object *target = nullptr;
	if (git_object_lookup(&target, repository, &tag.target, GIT_OBJECT_ANY) < 0) {
		git_error_clear();
		tag.summary = "(missing object)";
		return;
	}
	ObjectPtr targetPtr(target);
	git_object *commit = nullptr;
	if (git_object_peel(&commit, target, GIT_OBJECT_COMMIT) < 0) {
		git_error_clear();
		tag.summary = "(not a commit)";
		return;
	}
	ObjectPtr commitPtr(commit);
	tag.commit = *git_object_id(commit);
	tag.hasCommit = true;
	const CommitInfo info = ReadCommit(repository, tag.commit);
	tag.summary = info.summary;
	if (!tag.annotated) {
		tag.time = info.time;
	}
}


std::map<std::string, std::vector<std::string>> ReferenceLabels(git_repository *repository)
{
	struct Label {
		int group;
		std::string name;
	};
	std::map<std::string, std::vector<Label>> labels;
	git_reference_iterator *iterator = nullptr;
	Check(git_reference_iterator_new(&iterator, repository), "Cannot list the references");
	ReferenceIteratorPtr iteratorPtr(iterator);
	git_reference *reference = nullptr;
	while (git_reference_next(&reference, iterator) == 0) {
		ReferencePtr referencePtr(reference);
		if (git_reference_type(reference) != GIT_REFERENCE_DIRECT) {
			continue;
		}
		const std::string name = git_reference_name(reference);
		Label label;
		if (name.starts_with(kHeadsPrefix)) {
			label = {0, name.substr(sizeof(kHeadsPrefix) - 1)};
		} else if (name.starts_with(kRemotesPrefix)) {
			label = {1, name.substr(sizeof(kRemotesPrefix) - 1)};
		} else if (name.starts_with(kTagsPrefix)) {
			label = {2, "tags/" + name.substr(sizeof(kTagsPrefix) - 1)};
		} else {
			continue;
		}
		// Packed references keep the peeled commit, so this is fast even
		// for many tags.
		git_object *commit = nullptr;
		if (git_reference_peel(&commit, reference, GIT_OBJECT_COMMIT) < 0) {
			git_error_clear();
			continue;
		}
		ObjectPtr commitPtr(commit);
		labels[OidString(*git_object_id(commit))].push_back(std::move(label));
	}

	std::map<std::string, std::vector<std::string>> result;
	for (auto &[commit, commitLabels] : labels) {
		std::sort(commitLabels.begin(), commitLabels.end(), [](const Label &a, const Label &b) {
			return a.group != b.group ? a.group < b.group : NaturalLess(a.name, b.name);
		});
		std::vector<std::string> &names = result[commit];
		for (const Label &label : commitLabels) {
			names.push_back(label.name);
		}
	}
	return result;
}


static bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}


bool NaturalLess(const std::string &a, const std::string &b)
{
	size_t i = 0;
	size_t j = 0;
	while (i < a.size() && j < b.size()) {
		if (IsDigit(a[i]) && IsDigit(b[j])) {
			// Numbers compare by value: without leading zeros, the longer one
			// is larger, and equal lengths compare digit by digit.
			size_t startA = i;
			size_t startB = j;
			while (startA < a.size() && a[startA] == '0') {
				startA++;
			}
			while (startB < b.size() && b[startB] == '0') {
				startB++;
			}
			size_t endA = startA;
			size_t endB = startB;
			while (endA < a.size() && IsDigit(a[endA])) {
				endA++;
			}
			while (endB < b.size() && IsDigit(b[endB])) {
				endB++;
			}
			if (endA - startA != endB - startB) {
				return endA - startA < endB - startB;
			}
			const int order = a.compare(startA, endA - startA, b, startB, endB - startB);
			if (order != 0) {
				return order < 0;
			}
			// Equal values: fewer leading zeros first.
			if (startA - i != startB - j) {
				return startA - i < startB - j;
			}
			i = endA;
			j = endB;
			continue;
		}
		if (a[i] != b[j]) {
			return static_cast<unsigned char>(a[i]) < static_cast<unsigned char>(b[j]);
		}
		i++;
		j++;
	}
	return a.size() - i < b.size() - j;
}


Snapshot Snapshot::Load(git_repository *repository)
{
	Snapshot snapshot;
	snapshot.worktrees = LoadWorktrees(repository);
	snapshot.branches = LoadBranches(repository, snapshot.worktrees);
	snapshot.remotes = LoadRemotes(repository);
	snapshot.tags = LoadTags(repository);
	return snapshot;
}


const WorktreeInfo *Snapshot::FindWorktree(const std::string &path) const
{
	for (const WorktreeInfo &worktree : worktrees) {
		if (worktree.path == path) {
			return &worktree;
		}
	}
	return nullptr;
}


const WorktreeInfo *Snapshot::WorktreeOfBranch(const std::string &branch) const
{
	for (const WorktreeInfo &worktree : worktrees) {
		if (worktree.branch == branch) {
			return &worktree;
		}
	}
	return nullptr;
}


const BranchInfo *Snapshot::FindBranch(const std::string &name) const
{
	for (const BranchInfo &branch : branches) {
		if (branch.name == name) {
			return &branch;
		}
	}
	return nullptr;
}


const RemoteInfo *Snapshot::FindRemote(const std::string &name) const
{
	for (const RemoteInfo &remote : remotes) {
		if (remote.name == name) {
			return &remote;
		}
	}
	return nullptr;
}


const RemoteCopyInfo *Snapshot::FindRemoteCopy(const std::string &name) const
{
	for (const RemoteInfo &remote : remotes) {
		for (const RemoteCopyInfo &copy : remote.copies) {
			if (copy.name == name) {
				return &copy;
			}
		}
	}
	return nullptr;
}


const TagInfo *Snapshot::FindTag(const std::string &name) const
{
	for (const TagInfo &tag : tags) {
		if (tag.name == name) {
			return &tag;
		}
	}
	return nullptr;
}


} // GitTool
