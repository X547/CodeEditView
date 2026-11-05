#pragma once

#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Git.h"


namespace GitDiff {


using namespace DiffKit;


// One changed file, like a line of `git status --short`.
struct StatusEntry {
	std::string path;
	// Kind of change between HEAD and the index ("staged") and between the
	// index and the working tree ("unstaged"): 'A', 'M', 'D', 'T', 'R', '?'
	// (untracked), or ' ' for none.
	char staged = ' ';
	char unstaged = ' ';
	bool conflicted {};

	bool operator==(const StatusEntry &other) const = default;
};


struct FileContent {
	std::string data;
	// Git file mode (GIT_FILEMODE_*).
	uint32_t mode = GIT_FILEMODE_BLOB;
};


// A repository with a working tree. Paths are relative to the working tree
// root, with '/' separators. Errors throw GitError.
class Repository {
private:
	RepositoryPtr fRepository;
	// With a trailing '/'.
	std::string fWorkdir;

	IndexPtr Index();

public:
	// Opens the repository containing path (a directory or a file).
	explicit Repository(const std::string &path);

	git_repository *Get() const {return fRepository.get();}
	const std::string &Workdir() const {return fWorkdir;}
	// The .git directory.
	std::string GitDir() const;
	// Path relative to the working tree root, if it is inside it.
	std::optional<std::string> RelativePath(const std::string &absolutePath) const;

	std::vector<StatusEntry> Status();

	std::optional<FileContent> ReadHead(const std::string &path);
	// The stage 0 entry; none for a new or a conflicted file.
	std::optional<FileContent> ReadIndex(const std::string &path);
	bool IsConflicted(const std::string &path);
	std::optional<FileContent> ReadWorktree(const std::string &path);

	// Stores data as the file's blob in the index and writes the index. The
	// entry's stat data is cleared, so git compares the working tree file
	// by content.
	void WriteIndex(const std::string &path, std::string_view data, uint32_t mode);
	void RemoveFromIndex(const std::string &path);
	// Replaces the file through a temporary file, keeping its permissions.
	void WriteWorktree(const std::string &path, std::string_view data);

	// Working tree data converted as `git add` would store it (line ending
	// conversion).
	std::string CleanWorktreeData(const std::string &path, std::string_view data);
	// Why the file cannot be edited as text here (filter attributes); empty
	// if it can.
	std::string FilterProblem(const std::string &path);
};


} // GitDiff
