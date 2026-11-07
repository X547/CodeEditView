#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include <git2.h>
#include <git2/sys/errors.h>


namespace DiffKit {


class GitError: public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};


// Throws GitError with libgit2's last error message if result < 0.
void Check(int result, const char *what);


template<typename T, void (*Free)(T*)>
struct GitDeleter {
	void operator()(T *object) const {Free(object);}
};

template<typename T, void (*Free)(T*)>
using GitPtr = std::unique_ptr<T, GitDeleter<T, Free>>;

using RepositoryPtr = GitPtr<git_repository, git_repository_free>;
using IndexPtr = GitPtr<git_index, git_index_free>;
using ReferencePtr = GitPtr<git_reference, git_reference_free>;
using ObjectPtr = GitPtr<git_object, git_object_free>;
using TreeEntryPtr = GitPtr<git_tree_entry, git_tree_entry_free>;
using BlobPtr = GitPtr<git_blob, git_blob_free>;
using StatusListPtr = GitPtr<git_status_list, git_status_list_free>;
using FilterListPtr = GitPtr<git_filter_list, git_filter_list_free>;
using CommitPtr = GitPtr<git_commit, git_commit_free>;
using TreePtr = GitPtr<git_tree, git_tree_free>;
using TagPtr = GitPtr<git_tag, git_tag_free>;
using DiffPtr = GitPtr<git_diff, git_diff_free>;
using RevwalkPtr = GitPtr<git_revwalk, git_revwalk_free>;
using WorktreePtr = GitPtr<git_worktree, git_worktree_free>;
using RemotePtr = GitPtr<git_remote, git_remote_free>;
using ReflogPtr = GitPtr<git_reflog, git_reflog_free>;
using ConfigPtr = GitPtr<git_config, git_config_free>;
using SignaturePtr = GitPtr<git_signature, git_signature_free>;
using RebasePtr = GitPtr<git_rebase, git_rebase_free>;
using BranchIteratorPtr = GitPtr<git_branch_iterator, git_branch_iterator_free>;
using ReferenceIteratorPtr = GitPtr<git_reference_iterator, git_reference_iterator_free>;


// Owns a git_buf.
class GitBuf {
private:
	git_buf fBuf {};

public:
	GitBuf() = default;
	~GitBuf() {git_buf_dispose(&fBuf);}

	GitBuf(const GitBuf &other) = delete;
	GitBuf &operator=(const GitBuf &other) = delete;

	git_buf *Get() {return &fBuf;}
	std::string String() const {return fBuf.ptr != nullptr ? std::string(fBuf.ptr, fBuf.size) : std::string();}
};


// Owns the strings of a git_strarray filled by libgit2.
class GitStrArray {
private:
	git_strarray fArray {};

public:
	GitStrArray() = default;
	~GitStrArray() {git_strarray_dispose(&fArray);}

	GitStrArray(const GitStrArray &other) = delete;
	GitStrArray &operator=(const GitStrArray &other) = delete;

	git_strarray *Get() {return &fArray;}
	size_t Count() const {return fArray.count;}
	const char *At(size_t index) const {return fArray.strings[index];}
};


// Initializes libgit2 for its lifetime.
class GitLibrary {
public:
	GitLibrary() {git_libgit2_init();}
	~GitLibrary() {git_libgit2_shutdown();}

	GitLibrary(const GitLibrary &other) = delete;
	GitLibrary &operator=(const GitLibrary &other) = delete;
};


} // DiffKit
