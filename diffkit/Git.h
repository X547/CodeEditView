#pragma once

#include <memory>
#include <stdexcept>
#include <string>

#include <git2.h>


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


// Initializes libgit2 for its lifetime.
class GitLibrary {
public:
	GitLibrary() {git_libgit2_init();}
	~GitLibrary() {git_libgit2_shutdown();}

	GitLibrary(const GitLibrary &other) = delete;
	GitLibrary &operator=(const GitLibrary &other) = delete;
};


} // DiffKit
