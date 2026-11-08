#include "FileProvider.h"

#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include <algorithm>

#include "FileIO.h"


namespace Workspace {


bool FileProvider::IsWritable(const Locator &) const
{
	return true;
}


Locator FileProvider::Resolve(const Locator &locator)
{
	return locator;
}


std::string FileProvider::Version(const std::string &path)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		if (errno == ENOENT || errno == ENOTDIR) {
			return {};
		}
		throw ProviderError(DiffKit::ErrnoMessage("Cannot read", path));
	}
	return std::to_string(info.st_mtim.tv_sec) + "." + std::to_string(info.st_mtim.tv_nsec) + ":"
		+ std::to_string(info.st_size) + ":" + std::to_string(info.st_ino);
}


FileData FileProvider::Read(const Locator &locator)
{
	const std::string &path = locator.Path();
	try {
		FileData result;
		// A file replaced while reading gets read again.
		for (int attempt = 0; attempt < 3; attempt++) {
			const std::string before = Version(path);
			result.data = DiffKit::ReadFile(path);
			result.version = Version(path);
			if (result.version == before) {
				break;
			}
		}
		if (!result.data.has_value()) {
			result.version.clear();
		}
		return result;
	} catch (const DiffKit::FileError &error) {
		throw ProviderError(error.what());
	}
}


std::string FileProvider::Write(const Locator &locator, std::string_view data, const std::string &expectedVersion,
	bool overwrite)
{
	const std::string &path = locator.Path();
	if (!overwrite && Version(path) != expectedVersion) {
		throw VersionConflict(path + " was changed by another program");
	}
	try {
		DiffKit::WriteFile(path, data);
	} catch (const DiffKit::FileError &error) {
		throw ProviderError(error.what());
	}
	return Version(path);
}


std::vector<DirEntry> FileProvider::List(const Locator &directory)
{
	const std::string &path = directory.Path();
	DIR *dir = opendir(path.c_str());
	if (dir == nullptr) {
		throw ProviderError(DiffKit::ErrnoMessage("Cannot list", path));
	}
	std::vector<DirEntry> entries;
	while (const struct dirent *entry = readdir(dir)) {
		const std::string name = entry->d_name;
		if (name == "." || name == "..") {
			continue;
		}
		struct stat info;
		const std::string entryPath = path == "/" ? "/" + name : path + "/" + name;
		const bool isDirectory = stat(entryPath.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
		entries.push_back({name, isDirectory});
	}
	closedir(dir);
	std::sort(entries.begin(), entries.end(), [](const DirEntry &a, const DirEntry &b) {
		return a.name < b.name;
	});
	return entries;
}


} // Workspace
