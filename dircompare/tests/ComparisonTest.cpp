#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Comparison.h"
#include "TempDirectory.h"


namespace DirCompare {


static ScanEntry Entry(const std::string &path, EntryType left, EntryType right, EntryStatus status)
{
	ScanEntry entry;
	entry.path = path;
	entry.sides[0].type = left;
	entry.sides[1].type = right;
	entry.status = status;
	return entry;
}


static std::vector<std::string> ChildNames(const Node &node)
{
	std::vector<std::string> names;
	for (const auto &child : node.children) {
		names.push_back(child->name);
	}
	return names;
}


TEST_CASE("comparison tree")
{
	constexpr EntryType kFile = EntryType::File;
	constexpr EntryType kDirectory = EntryType::Directory;
	constexpr EntryType kNone = EntryType::None;

	Comparison comparison;
	comparison.AddEntries({
		Entry("a.txt", kFile, kFile, EntryStatus::Same),
		Entry("b.txt", kFile, kFile, EntryStatus::Pending),
		Entry("sub", kDirectory, kDirectory, EntryStatus::Same),
		Entry("z", kDirectory, kDirectory, EntryStatus::Same),
	});
	comparison.UpdateStatuses();
	Node &root = comparison.Root();
	// Directories first.
	CHECK(ChildNames(root) == std::vector<std::string> {"sub", "z", "a.txt", "b.txt"});
	// Directories not listed yet are pending.
	CHECK(comparison.Find("sub")->shown == EntryStatus::Pending);
	CHECK(root.shown == EntryStatus::Pending);

	comparison.AddEntries({Entry("sub/c.txt", kFile, kFile, EntryStatus::Same)});
	comparison.ApplyChanges({{"b.txt", EntryStatus::Same}});
	std::vector<Node*> changed = comparison.UpdateStatuses();
	CHECK(comparison.Find("sub")->shown == EntryStatus::Same);
	CHECK(comparison.Find("b.txt")->shown == EntryStatus::Same);
	CHECK(comparison.Find("z")->shown == EntryStatus::Pending);
	CHECK(changed.size() == 2);

	comparison.SetComplete();
	comparison.UpdateStatuses();
	CHECK(comparison.Find("z")->shown == EntryStatus::Same);
	CHECK(root.shown == EntryStatus::Same);

	comparison.AddEntries({Entry("z/new.txt", kFile, kNone, EntryStatus::LeftOnly)});
	comparison.UpdateStatuses();
	CHECK(comparison.Find("z")->shown == EntryStatus::Different);
	CHECK(root.shown == EntryStatus::Different);

	// A rescan after copying z to the right.
	comparison.ReplaceSubtree("z", Entry("z", kDirectory, kDirectory, EntryStatus::Same),
		{Entry("z/new.txt", kFile, kFile, EntryStatus::Same)});
	comparison.UpdateStatuses();
	CHECK(comparison.Find("z")->shown == EntryStatus::Same);
	CHECK(root.shown == EntryStatus::Same);
	CHECK(ChildNames(root) == std::vector<std::string> {"sub", "z", "a.txt", "b.txt"});

	// A file replaced by a directory moves to the directories.
	comparison.ReplaceSubtree("a.txt", Entry("a.txt", kDirectory, kDirectory, EntryStatus::Same), {});
	comparison.UpdateStatuses();
	CHECK(ChildNames(root) == std::vector<std::string> {"a.txt", "sub", "z", "b.txt"});
	CHECK(comparison.Find("a.txt")->shown == EntryStatus::Same);

	// Deleted from both sides.
	comparison.ReplaceSubtree("sub", std::nullopt, {});
	CHECK(comparison.Find("sub") == nullptr);
	CHECK(comparison.Find("sub/c.txt") == nullptr);
	CHECK(ChildNames(root) == std::vector<std::string> {"a.txt", "z", "b.txt"});

	// The whole tree.
	comparison.ReplaceSubtree("", std::nullopt, {Entry("only.txt", kNone, kFile, EntryStatus::RightOnly)});
	comparison.UpdateStatuses();
	CHECK(ChildNames(root) == std::vector<std::string> {"only.txt"});
	CHECK(comparison.Find("z") == nullptr);
	CHECK(root.shown == EntryStatus::Different);
}


TEST_CASE("building a comparison")
{
	TempDirectory left;
	TempDirectory right;
	left.Write("a.txt", "abc\n");
	right.Write("a.txt", "abd\n");
	left.Write("same.txt", "same\n");
	right.Write("same.txt", "same\n");
	left.Write("dir/b.txt", "b\n");

	std::vector<BuildProgress> reports;
	const auto report = [&reports](const BuildProgress &progress) {
		reports.push_back(progress);
	};
	const std::atomic<bool> cancel(false);
	std::unique_ptr<Comparison> comparison = BuildComparison(left.Path(), right.Path(), ScanOptions(), cancel, report);
	REQUIRE(comparison != nullptr);
	CHECK(comparison->IsComplete());
	CHECK(comparison->Find("a.txt")->shown == EntryStatus::Different);
	CHECK(comparison->Find("same.txt")->shown == EntryStatus::Same);
	CHECK(comparison->Find("dir")->shown == EntryStatus::LeftOnly);
	CHECK(comparison->Find("dir/b.txt")->shown == EntryStatus::LeftOnly);
	CHECK(comparison->Root().shown == EntryStatus::Different);

	REQUIRE_FALSE(reports.empty());
	const BuildProgress &last = reports.back();
	CHECK(last.entries == 4);
	CHECK(last.filesToCompare == 2);
	CHECK(last.filesCompared == 2);
	CHECK(last.bytesToCompare == 9);
	CHECK(last.bytesCompared == 9);
	CHECK(last.comparingContents);
	CHECK_FALSE(reports.front().comparingContents);

	const std::atomic<bool> cancelled(true);
	CHECK(BuildComparison(left.Path(), right.Path(), ScanOptions(), cancelled, report) == nullptr);
	CHECK_THROWS(BuildComparison(left / "missing", right.Path(), ScanOptions(), cancel, report));
}


} // DirCompare
