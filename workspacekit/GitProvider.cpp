#include "GitProvider.h"

#include <algorithm>

#include <Autolock.h>


namespace Workspace {


using DiffKit::Check;
using DiffKit::GitError;


static std::string OidString(const git_oid *id)
{
	char buffer[GIT_OID_HEXSZ + 1];
	git_oid_tostr(buffer, sizeof(buffer), id);
	return buffer;
}


// Runs a libgit2 operation, turning its errors into provider errors.
template<typename Function>
static auto Guard(Function function)
{
	try {
		return function();
	} catch (const GitError &error) {
		throw ProviderError(error.what());
	}
}


GitProvider::GitProvider():
	fLock("GitProvider")
{
}


git_repository *GitProvider::Open(const std::string &directory)
{
	auto it = fRepositories.find(directory);
	if (it == fRepositories.end()) {
		git_repository *repository = nullptr;
		Check(git_repository_open_ext(&repository, directory.c_str(), 0, nullptr), "Cannot open the repository");
		it = fRepositories.emplace(directory, DiffKit::RepositoryPtr(repository)).first;
	}
	return it->second.get();
}


DiffKit::TreePtr GitProvider::Tree(git_repository *repository, const Locator &locator)
{
	git_object *object = nullptr;
	const std::string spec = locator.Revision() + "^{tree}";
	Check(git_revparse_single(&object, repository, spec.c_str()), "Cannot find the commit");
	return DiffKit::TreePtr(reinterpret_cast<git_tree*>(object));
}


bool GitProvider::IsWritable(const Locator &) const
{
	return false;
}


Locator GitProvider::Resolve(const Locator &locator)
{
	BAutolock lock(fLock);
	return Guard([&] {
		git_repository *repository = Open(locator.Repository());
		git_object *object = nullptr;
		const std::string spec = locator.Revision() + "^{commit}";
		Check(git_revparse_single(&object, repository, spec.c_str()), "Cannot find the commit");
		DiffKit::ObjectPtr commit(object);

		const char *workdir = git_repository_workdir(repository);
		const std::string top = workdir != nullptr ? workdir : git_repository_path(repository);
		return Locator::GitCommit(top, OidString(git_object_id(object)), locator.Path());
	});
}


FileInfo GitProvider::Read(const Locator &locator, DataSink &sink)
{
	if (locator.Path().empty()) {
		throw ProviderError(locator.DisplayName() + " is a directory");
	}
	BAutolock lock(fLock);
	return Guard([&] {
		git_repository *repository = Open(locator.Repository());
		DiffKit::TreePtr tree = Tree(repository, locator);
		git_tree_entry *entry = nullptr;
		const int result = git_tree_entry_bypath(&entry, tree.get(), locator.Path().c_str());
		if (result == GIT_ENOTFOUND) {
			return FileInfo {};
		}
		Check(result, "Cannot read the tree");
		DiffKit::TreeEntryPtr entryPtr(entry);
		if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB) {
			throw ProviderError(locator.DisplayName() + " is not a file");
		}
		git_blob *blob = nullptr;
		Check(git_blob_lookup(&blob, repository, git_tree_entry_id(entry)), "Cannot read the file");
		DiffKit::BlobPtr blobPtr(blob);
		sink.Append(std::string_view(static_cast<const char*>(git_blob_rawcontent(blob)), git_blob_rawsize(blob)));
		FileInfo info;
		info.exists = true;
		info.version = OidString(git_tree_entry_id(entry));
		return info;
	});
}


std::string GitProvider::Write(const Locator &locator, const DiffKit::ChunkSource &source,
	const std::string &expectedVersion, bool overwrite)
{
	(void)locator;
	(void)source;
	(void)expectedVersion;
	(void)overwrite;
	throw ProviderError("Files of a Git commit are read-only");
}


std::vector<DirEntry> GitProvider::List(const Locator &directory)
{
	BAutolock lock(fLock);
	return Guard([&] {
		git_repository *repository = Open(directory.Repository());
		DiffKit::TreePtr tree = Tree(repository, directory);
		if (!directory.Path().empty()) {
			git_tree_entry *entry = nullptr;
			Check(git_tree_entry_bypath(&entry, tree.get(), directory.Path().c_str()), "Cannot find the directory");
			DiffKit::TreeEntryPtr entryPtr(entry);
			if (git_tree_entry_type(entry) != GIT_OBJECT_TREE) {
				throw ProviderError(directory.DisplayName() + " is not a directory");
			}
			git_tree *subtree = nullptr;
			Check(git_tree_lookup(&subtree, repository, git_tree_entry_id(entry)), "Cannot read the directory");
			tree.reset(subtree);
		}
		git_odb *odb = nullptr;
		Check(git_repository_odb(&odb, repository), "Cannot read the repository");
		DiffKit::OdbPtr odbPtr(odb);
		std::vector<DirEntry> entries;
		for (size_t i = 0; i < git_tree_entrycount(tree.get()); i++) {
			const git_tree_entry *entry = git_tree_entry_byindex(tree.get(), i);
			DirEntry item;
			item.name = git_tree_entry_name(entry);
			item.directory = git_tree_entry_type(entry) == GIT_OBJECT_TREE;
			item.link = git_tree_entry_filemode(entry) == GIT_FILEMODE_LINK;
			// The header only: the size without reading the data.
			size_t size = 0;
			git_object_t type {};
			if (git_tree_entry_type(entry) == GIT_OBJECT_BLOB && !item.link
				&& git_odb_read_header(&size, &type, odb, git_tree_entry_id(entry)) == 0) {
				item.size = size;
			}
			entries.push_back(std::move(item));
		}
		std::sort(entries.begin(), entries.end(), [](const DirEntry &a, const DirEntry &b) {
			return a.name < b.name;
		});
		return entries;
	});
}


} // Workspace
