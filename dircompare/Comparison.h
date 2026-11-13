#pragma once

#include <sys/types.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Compare.h"
#include "Scanner.h"


namespace DirCompare {


// An entry of the merged tree of both sides.
struct Node {
	// Relative path; empty for the root.
	std::string path;
	std::string name;
	EntryInfo sides[2];
	// The status of the entry itself.
	EntryStatus own = EntryStatus::Same;
	// With the contents of a directory (AggregateStatus).
	EntryStatus shown = EntryStatus::Same;
	// The contents of a directory were reported.
	bool listed {};
	Node *parent {};
	// Directories first, then by name.
	std::vector<std::unique_ptr<Node>> children;

	// A directory on either side.
	bool IsDirectory() const
	{
		return sides[0].type == EntryType::Directory || sides[1].type == EntryType::Directory;
	}
};


// The result of comparing two directories, built from scan results.
class Comparison {
private:
	Node fRoot;
	std::unordered_map<std::string, Node*> fNodes;
	bool fComplete {};

	Node *Add(const ScanEntry &entry);
	void Forget(Node &node);
	bool UpdateNode(Node &node, std::vector<Node*> &changed);

public:
	Comparison();

	Node &Root() {return fRoot;}
	// The node at a relative path ("" is the root); null if none.
	Node *Find(const std::string &path);
	// Whether the scan finished, so all directories are listed.
	bool IsComplete() const {return fComplete;}

	// Adds the entries of a scan; returns the new nodes. The parent of an
	// entry must be known.
	std::vector<Node*> AddEntries(const std::vector<ScanEntry> &entries);
	void ApplyChanges(const std::vector<StatusChange> &changes);
	void SetComplete();
	// Replaces a node and its contents with the results of a rescan of it;
	// without self (gone from both sides), the node is removed. Node
	// pointers into the subtree become invalid.
	void ReplaceSubtree(const std::string &path, const std::optional<ScanEntry> &self,
		const std::vector<ScanEntry> &contents);

	// Recomputes the shown statuses; returns the nodes whose shown status
	// changed. A directory whose contents were not reported yet is Pending.
	std::vector<Node*> UpdateStatuses();
};


// How far BuildComparison got.
struct BuildProgress {
	size_t entries {};
	// File pairs whose contents are compared, and their size (one side).
	size_t filesToCompare {};
	size_t filesCompared {};
	off_t bytesToCompare {};
	off_t bytesCompared {};
	// All entries are listed; contents are being compared.
	bool comparingContents {};
};


// Scans two roots (Scan) into a complete comparison with updated statuses;
// null if cancelled. report is called on the calling thread after each step
// of the scan. Throws FileError if a root cannot be read.
std::unique_ptr<Comparison> BuildComparison(
	const std::string &leftRoot,
	const std::string &rightRoot,
	const ScanOptions &options,
	const std::atomic<bool> &cancel,
	const std::function<void(const BuildProgress &progress)> &report
);


} // DirCompare
