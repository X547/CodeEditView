#pragma once

#include <map>
#include <string>
#include <vector>

#include "Git.h"
#include "NameOrder.h"


namespace GitDiff {


// The files of a folder tree, for a PathMenu. Folders are relative paths
// with '/' separators; "" is the root.
class PathSource {
public:
	struct Entries {
		std::vector<std::string> folders;
		std::vector<std::string> files;

		bool operator==(const Entries &other) const = default;
	};

	virtual ~PathSource() = default;

	// The names in a folder, each list sorted as Tracker sorts names;
	// called when a menu opens, possibly in the menu's thread.
	virtual Entries List(const std::string &folder) const = 0;
};


using DiffKit::NameLess;


// The files on disk under a folder (a working tree), read when listed.
// The .git folder is left out; a link is a file, as for Git.
class FolderPathSource: public PathSource {
private:
	// With a trailing '/'.
	std::string fRoot;

public:
	explicit FolderPathSource(const std::string &root);

	Entries List(const std::string &folder) const override;
};


// The files of commits, read once when created, so listing does not use
// the repository. Submodules are left out.
class TreePathSource: public PathSource {
private:
	std::map<std::string, Entries> fFolders;

public:
	// The union of the files of the trees of the commits.
	TreePathSource(git_repository *repository, const std::vector<git_oid> &commits);

	Entries List(const std::string &folder) const override;
};


} // GitDiff
