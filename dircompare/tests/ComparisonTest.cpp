#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Comparison.h"


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


} // DirCompare
