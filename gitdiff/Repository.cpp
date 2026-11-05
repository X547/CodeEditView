#include "Repository.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>


namespace GitDiff {


static std::string ErrnoMessage(const std::string &what, const std::string &path)
{
	return what + " " + path + ": " + strerror(errno);
}


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

	const int fd = open(fullPath.c_str(), O_RDONLY);
	if (fd < 0) {
		throw GitError(ErrnoMessage("Cannot open", fullPath));
	}
	char buffer[64 * 1024];
	for (;;) {
		const ssize_t size = read(fd, buffer, sizeof(buffer));
		if (size < 0) {
			close(fd);
			throw GitError(ErrnoMessage("Cannot read", fullPath));
		}
		if (size == 0) {
			break;
		}
		content.data.append(buffer, size);
	}
	close(fd);
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
	const std::string fullPath = fWorkdir + path;
	mode_t permissions = 0644;
	struct stat info;
	if (stat(fullPath.c_str(), &info) == 0) {
		permissions = info.st_mode & 07777;
	}

	const std::string tempPath = fullPath + ".gitdiff-tmp";
	const int fd = open(tempPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, permissions);
	if (fd < 0) {
		throw GitError(ErrnoMessage("Cannot create", tempPath));
	}
	size_t written = 0;
	while (written < data.size()) {
		const ssize_t size = write(fd, data.data() + written, data.size() - written);
		if (size < 0) {
			close(fd);
			unlink(tempPath.c_str());
			throw GitError(ErrnoMessage("Cannot write", tempPath));
		}
		written += size;
	}
	fchmod(fd, permissions);
	if (close(fd) != 0 || rename(tempPath.c_str(), fullPath.c_str()) != 0) {
		unlink(tempPath.c_str());
		throw GitError(ErrnoMessage("Cannot save", fullPath));
	}
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


bool Repository::IsBinary(std::string_view data)
{
	return data.substr(0, 8000).find('\0') != std::string_view::npos;
}


} // GitDiff
