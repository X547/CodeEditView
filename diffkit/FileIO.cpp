#include "FileIO.h"

#include <errno.h>
#include <fcntl.h>
#include <fs_attr.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>


namespace DiffKit {


std::string ErrnoMessage(const std::string &what, const std::string &path)
{
	return what + " " + path + ": " + strerror(errno);
}


std::optional<std::string> ReadFile(const std::string &path)
{
	const int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0) {
		if (errno == ENOENT || errno == ENOTDIR) {
			return std::nullopt;
		}
		throw FileError(ErrnoMessage("Cannot open", path));
	}
	std::string data;
	char buffer[64 * 1024];
	for (;;) {
		const ssize_t size = read(fd, buffer, sizeof(buffer));
		if (size < 0) {
			const int error = errno;
			close(fd);
			errno = error;
			throw FileError(ErrnoMessage("Cannot read", path));
		}
		if (size == 0) {
			break;
		}
		data.append(buffer, size);
	}
	close(fd);
	return data;
}


bool CopyAttributes(int fromFd, int toFd)
{
	DIR *attributes = fs_fopen_attr_dir(fromFd);
	if (attributes == nullptr) {
		// No attribute support on the source file system.
		return true;
	}
	bool ok = true;
	std::vector<char> buffer;
	while (struct dirent *entry = fs_read_attr_dir(attributes)) {
		attr_info info;
		if (fs_stat_attr(fromFd, entry->d_name, &info) != 0) {
			ok = false;
			continue;
		}
		buffer.resize(info.size);
		const ssize_t size = fs_read_attr(fromFd, entry->d_name, info.type, 0, buffer.data(), info.size);
		if (size != info.size
			|| fs_write_attr(toFd, entry->d_name, info.type, 0, buffer.data(), info.size) != info.size) {
			ok = false;
		}
	}
	fs_close_attr_dir(attributes);
	return ok;
}


// The file a path names: a symbolic link (also one whose target does not
// exist yet) is followed, so that saving keeps the link.
static std::string FinalPath(const std::string &path)
{
	std::string result = path;
	// Loops of links end like the kernel's lookup would.
	for (int depth = 0; depth < 16; depth++) {
		struct stat info;
		if (lstat(result.c_str(), &info) != 0 || !S_ISLNK(info.st_mode)) {
			break;
		}
		std::vector<char> target(info.st_size > 0 ? info.st_size + 1 : PATH_MAX);
		const ssize_t size = readlink(result.c_str(), target.data(), target.size());
		if (size < 0 || static_cast<size_t>(size) >= target.size()) {
			break;
		}
		const std::string link(target.data(), size);
		if (link.starts_with('/')) {
			result = link;
		} else {
			const size_t slash = result.rfind('/');
			result = (slash == std::string::npos ? std::string() : result.substr(0, slash + 1)) + link;
		}
	}
	return result;
}


void WriteFile(const std::string &requestedPath, std::string_view data)
{
	const std::string path = FinalPath(requestedPath);
	const size_t slash = path.rfind('/');
	const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash + 1);
	std::string tempPath = path + ".save-XXXXXX";
	int fd = mkstemp(tempPath.data());
	if (fd < 0 && errno == ENAMETOOLONG) {
		tempPath = directory + ".save-XXXXXX";
		fd = mkstemp(tempPath.data());
	}
	if (fd < 0) {
		throw FileError(ErrnoMessage("Cannot create a file in", directory));
	}
	auto fail = [&](const char *what, const std::string &failedPath) {
		const int error = errno;
		close(fd);
		unlink(tempPath.c_str());
		errno = error;
		throw FileError(ErrnoMessage(what, failedPath));
	};

	size_t written = 0;
	while (written < data.size()) {
		const ssize_t size = write(fd, data.data() + written, data.size() - written);
		if (size < 0) {
			fail("Cannot write", tempPath);
		}
		written += size;
	}

	// mkstemp creates the file with mode 0600.
	mode_t permissions = 0666;
	const int oldFd = open(path.c_str(), O_RDONLY);
	if (oldFd >= 0) {
		struct stat info;
		if (fstat(oldFd, &info) == 0) {
			permissions = info.st_mode & 07777;
			// Only possible for the owner's group or with privileges; the
			// file then belongs to whoever saves it.
			if (fchown(fd, info.st_uid, info.st_gid) != 0) {
				permissions &= ~(S_ISUID | S_ISGID);
			}
		}
		CopyAttributes(oldFd, fd);
		close(oldFd);
	} else {
		const mode_t mask = umask(0);
		umask(mask);
		permissions &= ~mask;
	}
	if (fchmod(fd, permissions) != 0) {
		fail("Cannot set the permissions of", tempPath);
	}
	// The data must be on disk before the rename makes it the file, or a crash
	// could leave the file empty.
	if (fsync(fd) != 0) {
		fail("Cannot write", tempPath);
	}
	if (close(fd) != 0) {
		const int error = errno;
		unlink(tempPath.c_str());
		errno = error;
		throw FileError(ErrnoMessage("Cannot write", tempPath));
	}
	if (rename(tempPath.c_str(), path.c_str()) != 0) {
		const int error = errno;
		unlink(tempPath.c_str());
		errno = error;
		throw FileError(ErrnoMessage("Cannot save", path));
	}
}


} // DiffKit
