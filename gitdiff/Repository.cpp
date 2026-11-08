#include "Repository.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>

#include "CommitMessage.h"
#include "FileIO.h"


namespace GitDiff {


Repository::Repository(const std::string &path)
{
	git_repository *repository = nullptr;
	Check(git_repository_open_ext(&repository, path.c_str(), 0, nullptr), "Cannot open the repository");
	fRepository.reset(repository);
	const char *workdir = git_repository_workdir(repository);
	if (workdir == nullptr) {
		throw GitError("The repository has no working tree");
	}
	fWorkdir = workdir;
	if (!fWorkdir.ends_with('/')) {
		fWorkdir += '/';
	}
}


std::string Repository::GitDir() const
{
	return git_repository_path(fRepository.get());
}


std::optional<std::string> Repository::RelativePath(const std::string &absolutePath) const
{
	if (!absolutePath.starts_with(fWorkdir)) {
		return std::nullopt;
	}
	return absolutePath.substr(fWorkdir.size());
}


IndexPtr Repository::Index()
{
	git_index *index = nullptr;
	Check(git_repository_index(&index, fRepository.get()), "Cannot open the index");
	IndexPtr result(index);
	// Picks up changes made by other programs.
	Check(git_index_read(index, false), "Cannot read the index");
	return result;
}


std::vector<StatusEntry> Repository::Status()
{
	git_status_options options = GIT_STATUS_OPTIONS_INIT;
	options.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
	options.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
		| GIT_STATUS_OPT_SORT_CASE_SENSITIVELY | GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
	git_status_list *list = nullptr;
	Check(git_status_list_new(&list, fRepository.get(), &options), "Cannot get the status");
	StatusListPtr listPtr(list);

	std::vector<StatusEntry> entries;
	const size_t count = git_status_list_entrycount(list);
	for (size_t i = 0; i < count; i++) {
		const git_status_entry *status = git_status_byindex(list, i);
		const unsigned flags = status->status;
		if ((flags & GIT_STATUS_IGNORED) != 0 || flags == GIT_STATUS_CURRENT) {
			continue;
		}
		const git_diff_delta *delta = status->head_to_index != nullptr ? status->head_to_index : status->index_to_workdir;
		if (delta == nullptr) {
			continue;
		}
		StatusEntry entry;
		entry.path = delta->new_file.path != nullptr ? delta->new_file.path : delta->old_file.path;
		if ((flags & GIT_STATUS_INDEX_NEW) != 0) {
			entry.staged = 'A';
		} else if ((flags & GIT_STATUS_INDEX_MODIFIED) != 0) {
			entry.staged = 'M';
		} else if ((flags & GIT_STATUS_INDEX_DELETED) != 0) {
			entry.staged = 'D';
		} else if ((flags & GIT_STATUS_INDEX_RENAMED) != 0) {
			entry.staged = 'R';
		} else if ((flags & GIT_STATUS_INDEX_TYPECHANGE) != 0) {
			entry.staged = 'T';
		}
		if ((flags & GIT_STATUS_WT_NEW) != 0) {
			entry.unstaged = '?';
		} else if ((flags & GIT_STATUS_WT_MODIFIED) != 0) {
			entry.unstaged = 'M';
		} else if ((flags & GIT_STATUS_WT_DELETED) != 0) {
			entry.unstaged = 'D';
		} else if ((flags & GIT_STATUS_WT_RENAMED) != 0) {
			entry.unstaged = 'R';
		} else if ((flags & GIT_STATUS_WT_TYPECHANGE) != 0) {
			entry.unstaged = 'T';
		}
		entry.conflicted = (flags & GIT_STATUS_CONFLICTED) != 0;
		entries.push_back(std::move(entry));
	}
	return entries;
}


std::optional<FileContent> Repository::ReadHead(const std::string &path)
{
	git_reference *head = nullptr;
	const int result = git_repository_head(&head, fRepository.get());
	if (result == GIT_EUNBORNBRANCH || result == GIT_ENOTFOUND) {
		return std::nullopt;
	}
	Check(result, "Cannot read HEAD");
	ReferencePtr headPtr(head);

	git_object *tree = nullptr;
	Check(git_reference_peel(&tree, head, GIT_OBJECT_TREE), "Cannot read the HEAD tree");
	ObjectPtr treePtr(tree);

	git_tree_entry *entry = nullptr;
	const int entryResult = git_tree_entry_bypath(&entry, reinterpret_cast<git_tree*>(tree), path.c_str());
	if (entryResult == GIT_ENOTFOUND) {
		return std::nullopt;
	}
	Check(entryResult, "Cannot find the file in HEAD");
	TreeEntryPtr entryPtr(entry);

	FileContent content;
	content.mode = git_tree_entry_filemode(entry);
	if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB) {
		return content;
	}
	git_blob *blob = nullptr;
	Check(git_blob_lookup(&blob, fRepository.get(), git_tree_entry_id(entry)), "Cannot read the HEAD blob");
	BlobPtr blobPtr(blob);
	content.data.assign(static_cast<const char*>(git_blob_rawcontent(blob)), git_blob_rawsize(blob));
	return content;
}


std::optional<FileContent> Repository::ReadIndex(const std::string &path)
{
	IndexPtr index = Index();
	const git_index_entry *entry = git_index_get_bypath(index.get(), path.c_str(), 0);
	if (entry == nullptr) {
		return std::nullopt;
	}
	FileContent content;
	content.mode = entry->mode;
	if (entry->mode == GIT_FILEMODE_COMMIT) {
		return content;
	}
	git_blob *blob = nullptr;
	Check(git_blob_lookup(&blob, fRepository.get(), &entry->id), "Cannot read the index blob");
	BlobPtr blobPtr(blob);
	content.data.assign(static_cast<const char*>(git_blob_rawcontent(blob)), git_blob_rawsize(blob));
	return content;
}


bool Repository::IsConflicted(const std::string &path)
{
	IndexPtr index = Index();
	for (int stage = 1; stage <= 3; stage++) {
		if (git_index_get_bypath(index.get(), path.c_str(), stage) != nullptr) {
			return true;
		}
	}
	return false;
}


std::optional<FileContent> Repository::ReadWorktree(const std::string &path)
{
	const std::string fullPath = fWorkdir + path;
	struct stat info;
	if (lstat(fullPath.c_str(), &info) != 0) {
		if (errno == ENOENT || errno == ENOTDIR) {
			return std::nullopt;
		}
		throw GitError(ErrnoMessage("Cannot read", fullPath));
	}

	FileContent content;
	if (S_ISLNK(info.st_mode)) {
		content.mode = GIT_FILEMODE_LINK;
		std::string target(info.st_size + 1, '\0');
		const ssize_t length = readlink(fullPath.c_str(), target.data(), target.size());
		if (length < 0) {
			throw GitError(ErrnoMessage("Cannot read the link", fullPath));
		}
		content.data.assign(target.data(), length);
		return content;
	}
	if (S_ISDIR(info.st_mode)) {
		content.mode = GIT_FILEMODE_COMMIT;
		return content;
	}
	content.mode = (info.st_mode & S_IXUSR) != 0 ? GIT_FILEMODE_BLOB_EXECUTABLE : GIT_FILEMODE_BLOB;

	std::optional<std::string> data = ReadFile(fullPath);
	if (!data.has_value()) {
		return std::nullopt;
	}
	content.data = std::move(*data);
	return content;
}


void Repository::WriteIndex(const std::string &path, std::string_view data, uint32_t mode)
{
	IndexPtr index = Index();
	git_index_entry entry {};
	if (const git_index_entry *existing = git_index_get_bypath(index.get(), path.c_str(), 0)) {
		entry = *existing;
	}
	entry.path = path.c_str();
	entry.mode = mode;
	entry.ctime = {};
	entry.mtime = {};
	entry.dev = 0;
	entry.ino = 0;
	entry.uid = 0;
	entry.gid = 0;
	entry.file_size = 0;
	Check(git_index_add_from_buffer(index.get(), &entry, data.data(), data.size()), "Cannot stage the file");
	Check(git_index_write(index.get()), "Cannot write the index");
}


void Repository::RemoveFromIndex(const std::string &path)
{
	IndexPtr index = Index();
	Check(git_index_remove_bypath(index.get(), path.c_str()), "Cannot remove the file from the index");
	Check(git_index_write(index.get()), "Cannot write the index");
}


void Repository::WriteWorktree(const std::string &path, std::string_view data)
{
	WriteFile(fWorkdir + path, data);
}


std::string Repository::CleanWorktreeData(const std::string &path, std::string_view data)
{
	git_filter_list *filters = nullptr;
	Check(git_filter_list_load(&filters, fRepository.get(), nullptr, path.c_str(), GIT_FILTER_TO_ODB, GIT_FILTER_DEFAULT),
		"Cannot load the filters");
	if (filters == nullptr) {
		return std::string(data);
	}
	FilterListPtr filtersPtr(filters);
	git_buf buffer {};
	Check(git_filter_list_apply_to_buffer(&buffer, filters, data.data(), data.size()), "Cannot filter the file");
	std::string result(buffer.ptr != nullptr ? buffer.ptr : "", buffer.size);
	git_buf_dispose(&buffer);
	return result;
}


std::string Repository::FilterProblem(const std::string &path)
{
	const char *value = nullptr;
	Check(git_attr_get(&value, fRepository.get(), 0, path.c_str(), "filter"), "Cannot read attributes");
	if (GIT_ATTR_HAS_VALUE(value)) {
		return std::string("uses the filter \"") + value + "\"";
	}
	Check(git_attr_get(&value, fRepository.get(), 0, path.c_str(), "ident"), "Cannot read attributes");
	if (GIT_ATTR_IS_TRUE(value)) {
		return "uses $Id$ expansion";
	}
	Check(git_attr_get(&value, fRepository.get(), 0, path.c_str(), "working-tree-encoding"), "Cannot read attributes");
	if (GIT_ATTR_HAS_VALUE(value)) {
		return "has a working tree encoding";
	}
	return {};
}


std::string Repository::HeadMessage()
{
	git_object *head = nullptr;
	if (git_revparse_single(&head, fRepository.get(), "HEAD^{commit}") < 0) {
		git_error_clear();
		return std::string();
	}
	ObjectPtr headPtr(head);
	const char *message = git_commit_message(reinterpret_cast<git_commit*>(head));
	return message != nullptr ? message : "";
}


bool Repository::CreatesChangeId()
{
	git_repository *repository = fRepository.get();
	git_config *config = nullptr;
	Check(git_repository_config_snapshot(&config, repository), "Cannot read the configuration");
	ConfigPtr configPtr(config);
	int enabled = 0;
	const int result = git_config_get_bool(&enabled, config, "gerrit.createChangeId");
	if (result == 0) {
		return enabled != 0;
	}
	git_error_clear();
	if (result != GIT_ENOTFOUND) {
		return false;
	}

	std::string hooks;
	const char *hooksPath = nullptr;
	if (git_config_get_string(&hooksPath, config, "core.hooksPath") == 0) {
		hooks = hooksPath;
		if (hooks.compare(0, 2, "~/") == 0) {
			const char *home = getenv("HOME");
			hooks = std::string(home != nullptr ? home : "") + hooks.substr(1);
		} else if (!hooks.starts_with('/')) {
			hooks = fWorkdir + hooks;
		}
		if (!hooks.ends_with('/')) {
			hooks += '/';
		}
	} else {
		git_error_clear();
		GitBuf path;
		Check(git_repository_item_path(path.Get(), repository, GIT_REPOSITORY_ITEM_HOOKS), "Cannot find the hooks");
		hooks = path.String();
	}
	try {
		const std::optional<std::string> hook = ReadFile(hooks + "commit-msg");
		return hook.has_value() && hook->find("Change-Id") != std::string::npos;
	} catch (const FileError &error) {
		(void)error;
		return false;
	}
}


void Repository::SetCreatesChangeId(bool enabled)
{
	git_config *config = nullptr;
	Check(git_repository_config(&config, fRepository.get()), "Cannot open the configuration");
	ConfigPtr configPtr(config);
	git_config *local = nullptr;
	Check(git_config_open_level(&local, config, GIT_CONFIG_LEVEL_LOCAL), "Cannot open the repository configuration");
	ConfigPtr localPtr(local);
	Check(git_config_set_bool(local, "gerrit.createChangeId", enabled), "Cannot change the configuration");
}


static std::string OidText(const git_oid &id)
{
	char text[GIT_OID_MAX_HEXSIZE + 1];
	git_oid_tostr(text, sizeof(text), &id);
	return text;
}


// Like Gerrit's hook, a hash of what the commit is made of; the committer
// time makes it unique.
static std::string NewChangeId(const git_oid &treeId, const git_commit *parent, const git_signature *committer,
	const std::string &message)
{
	std::string data = "tree " + OidText(treeId) + "\n";
	if (parent != nullptr) {
		data += "parent " + OidText(*git_commit_id(parent)) + "\n";
	}
	data += std::string("committer ") + committer->name + " <" + committer->email + "> "
		+ std::to_string(committer->when.time) + "\n\n" + message;
	git_oid id;
	Check(git_odb_hash(&id, data.data(), data.size(), GIT_OBJECT_BLOB), "Cannot make a Change-Id");
	return "I" + OidText(id);
}


static int AddMergeHead(const git_oid *id, void *data)
{
	static_cast<std::vector<git_oid>*>(data)->push_back(*id);
	return 0;
}


git_oid Repository::Commit(const std::string &message, bool amend)
{
	git_repository *repository = fRepository.get();
	GitBuf buffer;
	Check(git_message_prettify(buffer.Get(), message.c_str(), 0, '#'), "Cannot format the message");
	std::string cleanMessage = buffer.String();
	if (cleanMessage.empty()) {
		throw GitError("Enter a commit message");
	}

	IndexPtr index = Index();
	if (git_index_has_conflicts(index.get())) {
		throw GitError("Resolve the conflicts before committing");
	}
	git_oid treeId;
	Check(git_index_write_tree(&treeId, index.get()), "Cannot write the tree");
	git_tree *tree = nullptr;
	Check(git_tree_lookup(&tree, repository, &treeId), "Cannot read the tree");
	TreePtr treePtr(tree);

	git_signature *signature = nullptr;
	if (git_signature_default(&signature, repository) < 0) {
		git_error_clear();
		throw GitError("Set user.name and user.email in the Git configuration to commit");
	}
	SignaturePtr signaturePtr(signature);

	git_object *headObject = nullptr;
	if (git_revparse_single(&headObject, repository, "HEAD^{commit}") < 0) {
		git_error_clear();
		headObject = nullptr;
	}
	ObjectPtr headPtr(headObject);
	git_commit *head = reinterpret_cast<git_commit*>(headObject);

	if (CreatesChangeId()) {
		cleanMessage = AddChangeId(cleanMessage, NewChangeId(treeId, head, signature, cleanMessage));
	}

	git_oid id;
	if (amend) {
		if (head == nullptr) {
			throw GitError("There is no commit to amend");
		}
		Check(git_commit_amend(&id, head, "HEAD", nullptr, signature, nullptr, cleanMessage.c_str(), tree),
			"Cannot amend the commit");
		return id;
	}

	std::vector<git_oid> mergeHeads;
	const bool merging = git_repository_state(repository) == GIT_REPOSITORY_STATE_MERGE;
	if (merging) {
		Check(git_repository_mergehead_foreach(repository, AddMergeHead, &mergeHeads), "Cannot read the merge heads");
	}
	if (head != nullptr && mergeHeads.empty() && git_oid_equal(&treeId, git_commit_tree_id(head))) {
		throw GitError("Nothing is staged");
	}

	std::vector<CommitPtr> mergeCommits;
	std::vector<const git_commit*> parents;
	if (head != nullptr) {
		parents.push_back(head);
	}
	for (const git_oid &mergeHead : mergeHeads) {
		git_commit *commit = nullptr;
		Check(git_commit_lookup(&commit, repository, &mergeHead), "Cannot read the merged commit");
		mergeCommits.emplace_back(commit);
		parents.push_back(commit);
	}
	Check(git_commit_create(&id, repository, "HEAD", signature, signature, nullptr, cleanMessage.c_str(), tree,
		parents.size(), parents.data()), "Cannot create the commit");
	if (merging) {
		Check(git_repository_state_cleanup(repository), "Cannot clear the merge state");
	}
	return id;
}


} // GitDiff
