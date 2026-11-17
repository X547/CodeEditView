#include "PathSource.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <system_error>


namespace GitDiff {


using DiffKit::Check;


static void Sort(PathSource::Entries &entries)
{
	std::sort(entries.folders.begin(), entries.folders.end(), NameLess);
	std::sort(entries.files.begin(), entries.files.end(), NameLess);
}


//#pragma mark - FolderPathSource

FolderPathSource::FolderPathSource(const std::string &root):
	fRoot(root.empty() || root.back() == '/' ? root : root + "/")
{
}


PathSource::Entries FolderPathSource::List(const std::string &folder) const
{
	Entries entries;
	std::error_code error;
	std::filesystem::directory_iterator iterator(fRoot + folder, error);
	if (error) {
		return entries;
	}
	for (const std::filesystem::directory_entry &entry : iterator) {
		const std::string name = entry.path().filename().string();
		if (folder.empty() && name == ".git") {
			continue;
		}
		// A link is a file to Git, even a link to a folder.
		std::error_code typeError;
		if (entry.is_directory(typeError) && !entry.is_symlink(typeError)) {
			entries.folders.push_back(name);
		} else {
			entries.files.push_back(name);
		}
	}
	Sort(entries);
	return entries;
}


//#pragma mark - TreePathSource

struct TreeWalk {
	std::map<std::string, std::set<std::string>> folders;
	std::map<std::string, std::set<std::string>> files;
};


static int AddTreeEntry(const char *root, const git_tree_entry *entry, void *data)
{
	TreeWalk &walk = *static_cast<TreeWalk*>(data);
	std::string folder = root;
	if (!folder.empty() && folder.back() == '/') {
		folder.pop_back();
	}
	const char *name = git_tree_entry_name(entry);
	switch (git_tree_entry_type(entry)) {
		case GIT_OBJECT_TREE:
			walk.folders[folder].insert(name);
			break;
		case GIT_OBJECT_BLOB:
			walk.files[folder].insert(name);
			break;
		default:
			// Submodules are not files of this repository.
			break;
	}
	return 0;
}


TreePathSource::TreePathSource(git_repository *repository, const std::vector<git_oid> &commits)
{
	TreeWalk walk;
	for (const git_oid &id : commits) {
		git_commit *commit = nullptr;
		Check(git_commit_lookup(&commit, repository, &id), "Cannot read the commit");
		DiffKit::CommitPtr commitPtr(commit);
		git_tree *tree = nullptr;
		Check(git_commit_tree(&tree, commit), "Cannot read the tree");
		DiffKit::TreePtr treePtr(tree);
		Check(git_tree_walk(tree, GIT_TREEWALK_PRE, AddTreeEntry, &walk), "Cannot read the tree");
	}
	for (const auto &[folder, names] : walk.folders) {
		const std::string prefix = folder.empty() ? "" : folder + "/";
		Entries &entries = fFolders[folder];
		entries.folders.assign(names.begin(), names.end());
		// Every folder has its own entry, even if it has only folders.
		for (const std::string &name : names) {
			fFolders[prefix + name];
		}
	}
	for (const auto &[folder, names] : walk.files) {
		fFolders[folder].files.assign(names.begin(), names.end());
	}
	for (auto &[folder, entries] : fFolders) {
		Sort(entries);
	}
}


PathSource::Entries TreePathSource::List(const std::string &folder) const
{
	const auto found = fFolders.find(folder);
	return found != fFolders.end() ? found->second : Entries();
}



} // GitDiff
