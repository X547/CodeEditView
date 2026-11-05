#pragma once

#include <sys/types.h>
#include <time.h>

#include <atomic>
#include <string>
#include <vector>


namespace DirCompare {


enum class EntryType {
	// The entry does not exist on this side.
	None,
	File,
	Directory,
	Symlink,
	// Devices, FIFOs, sockets.
	Other,
	// lstat failed for another reason than a missing entry.
	Unreadable,
};


enum class EntryStatus {
	// Two files of the same size whose contents are not compared yet; a
	// directory with such files.
	Pending,
	Same,
	Different,
	LeftOnly,
	RightOnly,
	// A file on one side, a directory or link on the other.
	TypeMismatch,
	// Unreadable on a side.
	Error,
};


struct EntryInfo {
	EntryType type = EntryType::None;
	off_t size {};
	timespec modified {};
	mode_t mode {};
	// For symlinks.
	std::string linkTarget;
};


// lstat of a path (links are not followed).
EntryInfo ReadEntryInfo(const std::string &path);

// The status of a pair as far as it follows from the entry information:
// Pending for two files of the same, non-zero size. Two directories are
// Same; their contents decide in AggregateStatus.
EntryStatus InitialStatus(const EntryInfo &left, const EntryInfo &right);

// Compares two files byte by byte: Same, Different, or Error if one cannot
// be read. Pending if cancelled.
EntryStatus CompareFiles(const std::string &left, const std::string &right, const std::atomic<bool> *cancel = nullptr);

// The status shown for a directory pair: its own status if that is not
// Same, else Different if a child is not Same, Pending if a child is
// Pending, Same otherwise.
EntryStatus AggregateStatus(EntryStatus own, const std::vector<EntryStatus> &children);


} // DirCompare
