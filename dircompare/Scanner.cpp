#include "Scanner.h"

#include <dirent.h>
#include <errno.h>
#include <string.h>

#include <chrono>
#include <deque>
#include <exception>
#include <optional>
#include <set>

#include <FileIO.h>


namespace DirCompare {


using DiffKit::ErrnoMessage;
using DiffKit::FileError;


// Content results are reported at most this often.
static constexpr std::chrono::milliseconds kReportInterval(100);


static bool IsVcsName(const std::string &name)
{
	return name == ".git" || name == ".hg" || name == ".svn" || name == ".bzr";
}


// Names in a directory, without "." and ".."; none if it cannot be read.
static std::optional<std::set<std::string>> ListDirectory(const std::string &path, const ScanOptions &options)
{
	DIR *directory = opendir(path.c_str());
	if (directory == nullptr) {
		return std::nullopt;
	}
	std::set<std::string> names;
	while (struct dirent *entry = readdir(directory)) {
		const std::string name = entry->d_name;
		if (name == "." || name == ".." || (options.ignoreVcs && IsVcsName(name))) {
			continue;
		}
		names.insert(name);
	}
	closedir(directory);
	return names;
}


static std::string Join(const std::string &directory, const std::string &name)
{
	return directory.empty() ? name : directory + "/" + name;
}


bool Scan(const std::string &leftRoot, const std::string &rightRoot, const ScanOptions &options, ScanSink &sink,
	const std::atomic<bool> &cancel)
{
	const std::string roots[2] = {leftRoot, rightRoot};
	for (const std::string &root : roots) {
		DIR *directory = opendir(root.c_str());
		if (directory == nullptr) {
			throw FileError(ErrnoMessage("Cannot read the directory", root));
		}
		closedir(directory);
	}

	struct Directory {
		std::string path;
		bool onSide[2] {};
	};
	std::deque<Directory> queue;
	queue.push_back({"", {true, true}});
	std::vector<std::string> pending;

	while (!queue.empty()) {
		if (cancel.load()) {
			return false;
		}
		const Directory directory = queue.front();
		queue.pop_front();

		std::set<std::string> names;
		bool unreadable = false;
		for (int side = 0; side < 2; side++) {
			if (!directory.onSide[side]) {
				continue;
			}
			const std::optional<std::set<std::string>> listed
				= ListDirectory(Join(roots[side], directory.path), options);
			if (!listed.has_value()) {
				unreadable = true;
				continue;
			}
			names.insert(listed->begin(), listed->end());
		}
		if (unreadable && !directory.path.empty()) {
			sink.StatusesChanged({{directory.path, EntryStatus::Error}});
		}

		std::vector<ScanEntry> entries;
		for (const std::string &name : names) {
			ScanEntry entry;
			entry.path = Join(directory.path, name);
			Directory child {entry.path, {}};
			for (int side = 0; side < 2; side++) {
				if (directory.onSide[side]) {
					entry.sides[side] = ReadEntryInfo(roots[side] + "/" + entry.path);
				}
				child.onSide[side] = entry.sides[side].type == EntryType::Directory;
			}
			entry.status = InitialStatus(entry.sides[0], entry.sides[1]);
			if (entry.status == EntryStatus::Pending) {
				pending.push_back(entry.path);
			}
			if (child.onSide[0] || child.onSide[1]) {
				queue.push_back(child);
			}
			entries.push_back(std::move(entry));
		}
		if (!entries.empty()) {
			sink.EntriesFound(std::move(entries));
		}
	}

	std::vector<StatusChange> changes;
	auto lastReport = std::chrono::steady_clock::now();
	for (const std::string &path : pending) {
		const EntryStatus status = CompareFiles(roots[0] + "/" + path, roots[1] + "/" + path, &cancel);
		if (cancel.load()) {
			return false;
		}
		changes.emplace_back(path, status);
		const auto now = std::chrono::steady_clock::now();
		if (now - lastReport >= kReportInterval) {
			sink.StatusesChanged(std::move(changes));
			changes.clear();
			lastReport = now;
		}
	}
	if (!changes.empty()) {
		sink.StatusesChanged(std::move(changes));
	}
	return true;
}


ScanThread::ScanThread(const std::string &leftRoot, const std::string &rightRoot, const ScanOptions &options,
	const BMessenger &target):
	fTarget(target)
{
	fThread = std::thread([this, leftRoot, rightRoot, options]() {
		std::string error;
		try {
			Scan(leftRoot, rightRoot, options, *this, fCancel);
		} catch (const std::exception &e) {
			error = e.what();
		}
		{
			std::lock_guard<std::mutex> lock(fLock);
			fResults.finished = true;
			fResults.error = error;
		}
		Notify();
	});
}


ScanThread::~ScanThread()
{
	fCancel = true;
	fThread.join();
}


void ScanThread::Notify()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (fNotified) {
			return;
		}
		fNotified = true;
	}
	fTarget.SendMessage(kMsgScanProgress);
}


void ScanThread::EntriesFound(std::vector<ScanEntry> entries)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		for (ScanEntry &entry : entries) {
			fResults.entries.push_back(std::move(entry));
		}
	}
	Notify();
}


void ScanThread::StatusesChanged(std::vector<StatusChange> changes)
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		for (StatusChange &change : changes) {
			fResults.changes.push_back(std::move(change));
		}
	}
	Notify();
}


ScanThread::Results ScanThread::TakeResults()
{
	std::lock_guard<std::mutex> lock(fLock);
	Results results = std::move(fResults);
	fResults = Results();
	// A finished scan stays finished.
	fResults.finished = results.finished;
	fNotified = false;
	return results;
}


} // DirCompare
