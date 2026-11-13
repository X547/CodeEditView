#pragma once

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "Compare.h"


namespace DirCompare {


// An entry of either tree. path is relative to the roots, with '/'
// separators; the root itself is not an entry.
struct ScanEntry {
	std::string path;
	EntryInfo sides[2];
	EntryStatus status = EntryStatus::Pending;
};


using StatusChange = std::pair<std::string, EntryStatus>;


struct ScanOptions {
	// Skips .git, .hg, .svn and .bzr directories and files.
	bool ignoreVcs = true;
};


class ScanSink {
public:
	virtual ~ScanSink() = default;
	// The entries of one directory, sorted by name (bytes). A directory's
	// entry comes before its contents.
	virtual void EntriesFound(std::vector<ScanEntry> entries) = 0;
	// Later statuses: compared file contents, unreadable directories.
	virtual void StatusesChanged(std::vector<StatusChange> changes) = 0;
};


// Walks both roots breadth first, reporting all entries, then compares the
// contents of the Pending file pairs. Directories on one side only are
// walked too. Returns false if cancelled. Throws FileError if a root
// cannot be read. With start (a relative path), only the contents of that
// directory are scanned (on the sides where it is a directory).
bool Scan(const std::string &leftRoot, const std::string &rightRoot, const ScanOptions &options, ScanSink &sink,
	const std::atomic<bool> &cancel, const std::string &start = std::string());


} // DirCompare
