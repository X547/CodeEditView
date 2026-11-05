#include "FileOps.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <memory>

#include <FileIO.h>

#include "Compare.h"


namespace DirCompare {


using DiffKit::CopyAttributes;
using DiffKit::ErrnoMessage;


namespace {

struct Fd {
	int fd = -1;

	explicit Fd(int fd): fd(fd) {}
	~Fd()
	{
		if (fd >= 0) {
			close(fd);
		}
	}
	Fd(const Fd &other) = delete;
	Fd &operator=(const Fd &other) = delete;
};


std::string Parent(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
}


std::string Leaf(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


// Sets the modification time of an open node (the access time too).
void SetTimes(int fd, const struct stat &info)
{
	const timespec times[2] = {info.st_atim, info.st_mtim};
	futimens(fd, times);
}


bool CopyFile(const std::string &source, const std::string &target, std::vector<std::string> &errors)
{
	const Fd from(open(source.c_str(), O_RDONLY));
	struct stat info;
	if (from.fd < 0 || fstat(from.fd, &info) != 0) {
		errors.push_back(ErrnoMessage("Cannot read", source));
		return false;
	}
	std::string tempPath = Parent(target) + "/." + Leaf(target) + ".XXXXXX";
	const Fd to(mkstemp(tempPath.data()));
	if (to.fd < 0) {
		errors.push_back(ErrnoMessage("Cannot create a file in", Parent(target)));
		return false;
	}
	auto fail = [&](const char *what, const std::string &path) {
		errors.push_back(ErrnoMessage(what, path));
		unlink(tempPath.c_str());
		return false;
	};

	std::unique_ptr<char[]> buffer(new char[64 * 1024]);
	for (;;) {
		const ssize_t size = read(from.fd, buffer.get(), 64 * 1024);
		if (size < 0) {
			return fail("Cannot read", source);
		}
		if (size == 0) {
			break;
		}
		for (ssize_t written = 0; written < size;) {
			const ssize_t result = write(to.fd, buffer.get() + written, size - written);
			if (result < 0) {
				return fail("Cannot write", tempPath);
			}
			written += result;
		}
	}
	if (!CopyAttributes(from.fd, to.fd)) {
		errors.push_back("Some attributes of " + source + " were not copied");
	}
	if (fchmod(to.fd, info.st_mode & 07777) != 0) {
		return fail("Cannot set the permissions of", tempPath);
	}
	SetTimes(to.fd, info);
	if (rename(tempPath.c_str(), target.c_str()) != 0) {
		return fail("Cannot replace", target);
	}
	return true;
}


bool CopySymlink(const std::string &source, const std::string &target, std::vector<std::string> &errors)
{
	const EntryInfo info = ReadEntryInfo(source);
	if (info.type != EntryType::Symlink) {
		errors.push_back("Cannot read the link " + source);
		return false;
	}
	if (ReadEntryInfo(target).type != EntryType::None && unlink(target.c_str()) != 0) {
		errors.push_back(ErrnoMessage("Cannot replace", target));
		return false;
	}
	if (symlink(info.linkTarget.c_str(), target.c_str()) != 0) {
		errors.push_back(ErrnoMessage("Cannot create the link", target));
		return false;
	}
	return true;
}


bool CopyDirectory(const std::string &source, const std::string &target, std::vector<std::string> &errors)
{
	struct stat info;
	if (stat(source.c_str(), &info) != 0) {
		errors.push_back(ErrnoMessage("Cannot read", source));
		return false;
	}
	if (ReadEntryInfo(target).type != EntryType::Directory && mkdir(target.c_str(), 0700) != 0) {
		errors.push_back(ErrnoMessage("Cannot create the directory", target));
		return false;
	}

	bool ok = true;
	DIR *directory = opendir(source.c_str());
	if (directory == nullptr) {
		errors.push_back(ErrnoMessage("Cannot read the directory", source));
		return false;
	}
	while (struct dirent *entry = readdir(directory)) {
		const std::string name = entry->d_name;
		if (name == "." || name == "..") {
			continue;
		}
		ok = CopyEntry(source + "/" + name, target + "/" + name, errors) && ok;
	}
	closedir(directory);

	// The attributes, permissions and time last: copying the contents
	// changes the time, and the permissions may forbid writing.
	const Fd from(open(source.c_str(), O_RDONLY));
	const Fd to(open(target.c_str(), O_RDONLY));
	if (from.fd >= 0 && to.fd >= 0) {
		if (!CopyAttributes(from.fd, to.fd)) {
			errors.push_back("Some attributes of " + source + " were not copied");
		}
		if (fchmod(to.fd, info.st_mode & 07777) != 0) {
			errors.push_back(ErrnoMessage("Cannot set the permissions of", target));
			ok = false;
		}
		SetTimes(to.fd, info);
	}
	return ok;
}

}


bool CopyEntry(const std::string &source, const std::string &target, std::vector<std::string> &errors)
{
	const EntryInfo sourceInfo = ReadEntryInfo(source);
	const EntryInfo targetInfo = ReadEntryInfo(target);
	if (sourceInfo.type == EntryType::None || sourceInfo.type == EntryType::Unreadable) {
		errors.push_back(ErrnoMessage("Cannot read", source));
		return false;
	}
	if (targetInfo.type == EntryType::Unreadable) {
		errors.push_back(ErrnoMessage("Cannot read", target));
		return false;
	}
	if (targetInfo.type != EntryType::None && targetInfo.type != sourceInfo.type
		&& !DeleteEntry(target, errors)) {
		return false;
	}
	if (targetInfo.type == EntryType::None) {
		std::error_code error;
		std::filesystem::create_directories(Parent(target), error);
		if (error) {
			errors.push_back("Cannot create " + Parent(target) + ": " + error.message());
			return false;
		}
	}

	switch (sourceInfo.type) {
		case EntryType::File:
			return CopyFile(source, target, errors);
		case EntryType::Symlink:
			return CopySymlink(source, target, errors);
		case EntryType::Directory:
			return CopyDirectory(source, target, errors);
		default:
			errors.push_back("Cannot copy " + source + ": not a file, directory or link");
			return false;
	}
}


bool DeleteEntry(const std::string &path, std::vector<std::string> &errors)
{
	std::error_code error;
	std::filesystem::remove_all(path, error);
	if (error) {
		errors.push_back("Cannot delete " + path + ": " + error.message());
		return false;
	}
	return true;
}


} // DirCompare
