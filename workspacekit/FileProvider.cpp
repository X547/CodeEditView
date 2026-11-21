#include "FileProvider.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>

#include <Mime.h>
#include <Node.h>
#include <NodeInfo.h>
#include <fs_attr.h>

#include "FileIO.h"


namespace Workspace {


// Larger attributes of ours are not read.
static constexpr off_t kMaxAttributeSize = 1024 * 1024;


// The data of a B_MESSAGE_TYPE attribute; none if it is missing or too
// large.
static std::optional<std::string> ReadMessageAttribute(BNode &node, const char *name)
{
	attr_info info;
	if (node.InitCheck() != B_OK || node.GetAttrInfo(name, &info) != B_OK || info.type != B_MESSAGE_TYPE
		|| info.size <= 0 || info.size > kMaxAttributeSize) {
		return std::nullopt;
	}
	std::string data(info.size, '\0');
	if (node.ReadAttr(name, B_MESSAGE_TYPE, 0, data.data(), data.size()) != static_cast<ssize_t>(data.size())) {
		return std::nullopt;
	}
	return data;
}


static void CheckOpened(const BNode &node, const std::string &path)
{
	if (node.InitCheck() != B_OK) {
		throw ProviderError("Cannot open " + path + ": " + strerror(node.InitCheck()));
	}
}


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


FileInfo FileProvider::Read(const Locator &locator, DataSink &sink)
{
	const std::string &path = locator.Path();
	try {
		FileInfo result;
		// A file replaced while reading gets read again.
		for (int attempt = 0; attempt < 3; attempt++) {
			if (attempt > 0) {
				sink.Restart();
			}
			const std::string before = Version(path);
			result.exists = DiffKit::ReadFile(path, [&sink](std::string_view chunk) {
				sink.Append(chunk);
			});
			result.version = Version(path);
			if (result.version == before) {
				break;
			}
		}
		result.editorConfig = ReadEditorConfig(path);
		if (!result.exists) {
			result.version.clear();
			return result;
		}
		BNode node(path.c_str());
		BNodeInfo info(&node);
		char type[B_MIME_TYPE_LENGTH];
		if (node.InitCheck() == B_OK && info.InitCheck() == B_OK && info.GetType(type) == B_OK) {
			result.mimeType = type;
		}
		if (const std::optional<std::string> options = ReadMessageAttribute(node, kFileOptionsAttribute)) {
			result.options = UnflattenFileOptions(*options);
		}
		if (const std::optional<std::string> bookmarks = ReadMessageAttribute(node, kBookmarksAttribute)) {
			result.bookmarks = UnflattenBookmarks(*bookmarks);
		}
		return result;
	} catch (const DiffKit::FileError &error) {
		throw ProviderError(error.what());
	}
}


std::string FileProvider::Write(const Locator &locator, const DiffKit::ChunkSource &source,
	const std::string &expectedVersion, bool overwrite)
{
	const std::string &path = locator.Path();
	if (!overwrite && Version(path) != expectedVersion) {
		throw VersionConflict(path + " was changed by another program");
	}
	try {
		DiffKit::WriteFile(path, source);
	} catch (const DiffKit::FileError &error) {
		throw ProviderError(error.what());
	}
	return Version(path);
}


bool FileProvider::HasAttributes(const Locator &locator) const
{
	(void)locator;
	return true;
}


std::string FileProvider::WriteAttributes(const Locator &locator, const FileAttributes &attributes,
	const std::string &knownVersion)
{
	const std::string &path = locator.Path();
	const std::string before = Version(path);
	BNode node(path.c_str());
	CheckOpened(node, path);
	BNodeInfo info(&node);
	if (info.InitCheck() != B_OK) {
		throw ProviderError("Cannot open " + path + ": " + strerror(info.InitCheck()));
	}
	const char *type = attributes.mimeType.empty() ? nullptr : attributes.mimeType.c_str();
	if (info.SetType(type) != B_OK) {
		throw ProviderError("Cannot set the file type of " + path);
	}
	const std::string data = FlattenFileOptions(attributes.options);
	if (node.WriteAttr(kFileOptionsAttribute, B_MESSAGE_TYPE, 0, data.data(), data.size())
		!= static_cast<ssize_t>(data.size())) {
		throw ProviderError("Cannot write the options of " + path);
	}
	return before == knownVersion ? Version(path) : knownVersion;
}


std::string FileProvider::WriteBookmarks(const Locator &locator, const std::vector<size_t> &lines,
	const std::string &knownVersion)
{
	const std::string &path = locator.Path();
	const std::string before = Version(path);
	BNode node(path.c_str());
	CheckOpened(node, path);
	if (lines.empty()) {
		const status_t status = node.RemoveAttr(kBookmarksAttribute);
		if (status != B_OK && status != B_ENTRY_NOT_FOUND) {
			throw ProviderError("Cannot remove the bookmarks of " + path + ": " + strerror(status));
		}
	} else {
		const std::string data = FlattenBookmarks(lines);
		if (node.WriteAttr(kBookmarksAttribute, B_MESSAGE_TYPE, 0, data.data(), data.size())
			!= static_cast<ssize_t>(data.size())) {
			throw ProviderError("Cannot write the bookmarks of " + path);
		}
	}
	return before == knownVersion ? Version(path) : knownVersion;
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
		const std::string entryPath = path == "/" ? "/" + name : path + "/" + name;
		DirEntry item;
		item.name = name;
		struct stat info;
		if (lstat(entryPath.c_str(), &info) != 0) {
			entries.push_back(std::move(item));
			continue;
		}
		item.link = S_ISLNK(info.st_mode);
		// A link is described by its target; a broken one by itself.
		struct stat target;
		if (item.link && stat(entryPath.c_str(), &target) == 0) {
			info = target;
		}
		item.directory = S_ISDIR(info.st_mode);
		if (S_ISREG(info.st_mode)) {
			item.size = info.st_size;
		}
		item.modified = info.st_mtim.tv_sec;
		BNode node(entryPath.c_str());
		BNodeInfo nodeInfo(&node);
		char type[B_MIME_TYPE_LENGTH];
		if (node.InitCheck() == B_OK && nodeInfo.InitCheck() == B_OK && nodeInfo.GetType(type) == B_OK) {
			item.mimeType = type;
		}
		entries.push_back(std::move(item));
	}
	closedir(dir);
	std::sort(entries.begin(), entries.end(), [](const DirEntry &a, const DirEntry &b) {
		return a.name < b.name;
	});
	return entries;
}


} // Workspace
