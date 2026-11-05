#include "Compare.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>


namespace DirCompare {


EntryInfo ReadEntryInfo(const std::string &path)
{
	EntryInfo info;
	struct stat st;
	if (lstat(path.c_str(), &st) != 0) {
		info.type = errno == ENOENT || errno == ENOTDIR ? EntryType::None : EntryType::Unreadable;
		return info;
	}
	info.size = st.st_size;
	info.modified = st.st_mtim;
	info.mode = st.st_mode;
	if (S_ISREG(st.st_mode)) {
		info.type = EntryType::File;
	} else if (S_ISDIR(st.st_mode)) {
		info.type = EntryType::Directory;
	} else if (S_ISLNK(st.st_mode)) {
		info.type = EntryType::Symlink;
		std::string target(st.st_size + 1, '\0');
		const ssize_t length = readlink(path.c_str(), target.data(), target.size());
		if (length < 0) {
			info.type = EntryType::Unreadable;
		} else {
			info.linkTarget.assign(target.data(), length);
		}
	} else {
		info.type = EntryType::Other;
	}
	return info;
}


EntryStatus InitialStatus(const EntryInfo &left, const EntryInfo &right)
{
	if (left.type == EntryType::Unreadable || right.type == EntryType::Unreadable) {
		return EntryStatus::Error;
	}
	if (right.type == EntryType::None) {
		return EntryStatus::LeftOnly;
	}
	if (left.type == EntryType::None) {
		return EntryStatus::RightOnly;
	}
	if (left.type != right.type) {
		return EntryStatus::TypeMismatch;
	}
	switch (left.type) {
		case EntryType::File:
			if (left.size != right.size) {
				return EntryStatus::Different;
			}
			return left.size == 0 ? EntryStatus::Same : EntryStatus::Pending;
		case EntryType::Symlink:
			return left.linkTarget == right.linkTarget ? EntryStatus::Same : EntryStatus::Different;
		default:
			return EntryStatus::Same;
	}
}


namespace {

struct FileCloser {
	void operator()(int *fd) const
	{
		close(*fd);
		delete fd;
	}
};

using FdPtr = std::unique_ptr<int, FileCloser>;

FdPtr OpenFile(const std::string &path)
{
	const int fd = open(path.c_str(), O_RDONLY);
	return fd < 0 ? FdPtr() : FdPtr(new int(fd));
}

// Reads up to size bytes; fewer only at the end of the file. -1 on errors.
ssize_t ReadFully(int fd, char *buffer, size_t size)
{
	size_t done = 0;
	while (done < size) {
		const ssize_t result = read(fd, buffer + done, size - done);
		if (result < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (result == 0) {
			break;
		}
		done += result;
	}
	return static_cast<ssize_t>(done);
}

}


EntryStatus CompareFiles(const std::string &left, const std::string &right, const std::atomic<bool> *cancel)
{
	const FdPtr leftFd = OpenFile(left);
	const FdPtr rightFd = OpenFile(right);
	if (!leftFd || !rightFd) {
		return EntryStatus::Error;
	}
	constexpr size_t kBlockSize = 64 * 1024;
	std::unique_ptr<char[]> leftBuffer(new char[kBlockSize]);
	std::unique_ptr<char[]> rightBuffer(new char[kBlockSize]);
	for (;;) {
		if (cancel != nullptr && cancel->load()) {
			return EntryStatus::Pending;
		}
		const ssize_t leftSize = ReadFully(*leftFd, leftBuffer.get(), kBlockSize);
		const ssize_t rightSize = ReadFully(*rightFd, rightBuffer.get(), kBlockSize);
		if (leftSize < 0 || rightSize < 0) {
			return EntryStatus::Error;
		}
		if (leftSize != rightSize || memcmp(leftBuffer.get(), rightBuffer.get(), leftSize) != 0) {
			return EntryStatus::Different;
		}
		if (leftSize == 0) {
			return EntryStatus::Same;
		}
	}
}


EntryStatus AggregateStatus(EntryStatus own, const std::vector<EntryStatus> &children)
{
	if (own != EntryStatus::Same) {
		return own;
	}
	bool pending = false;
	for (const EntryStatus status : children) {
		if (status == EntryStatus::Pending) {
			pending = true;
		} else if (status != EntryStatus::Same) {
			return EntryStatus::Different;
		}
	}
	return pending ? EntryStatus::Pending : EntryStatus::Same;
}


} // DirCompare
