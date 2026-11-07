#pragma once

#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "Snapshot.h"


namespace GitTool {


struct Signature {
	std::string name;
	std::string email;
	int64_t time {};
};


struct CommitDetails {
	git_oid id {};
	std::string message;
	Signature author;
	Signature committer;
	std::vector<git_oid> parents;
};


// One file changed by a commit.
struct FileChange {
	// 'A', 'D', 'M', 'R' (renamed), 'C' (copied) or 'T' (type changed).
	char status = 'M';
	std::string oldPath;
	std::string newPath;
	// Zero when the file does not exist on that side.
	git_oid oldId {};
	git_oid newId {};
	uint32_t oldMode {};
	uint32_t newMode {};

	const std::string &Path() const {return newPath.empty() ? oldPath : newPath;}
};


CommitDetails ReadCommitDetails(git_repository *repository, const git_oid &id);

// The files a commit changes compared to one of its parents (0 for the
// first); a commit without parents is compared to an empty tree. Renames
// are detected.
std::vector<FileChange> CommitChanges(git_repository *repository, const git_oid &id, size_t parent);

// The content of a file version; none for a zero id. A submodule is shown
// as its commit.
std::optional<std::string> ReadFileVersion(git_repository *repository, const git_oid &id, uint32_t mode);


} // GitTool
