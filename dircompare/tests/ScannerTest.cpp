#include <unistd.h>

#include <atomic>
#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Compare.h"
#include "Scanner.h"
#include "TempDirectory.h"


namespace DirCompare {


namespace {

// Collects the reported statuses by path, in the order entries came.
class CollectingSink: public ScanSink {
public:
	std::vector<std::string> order;
	std::map<std::string, EntryStatus> statuses;

	void EntriesFound(std::vector<ScanEntry> entries) override
	{
		for (const ScanEntry &entry : entries) {
			order.push_back(entry.path);
			statuses[entry.path] = entry.status;
		}
	}

	void StatusesChanged(std::vector<StatusChange> changes) override
	{
		for (const StatusChange &change : changes) {
			statuses[change.first] = change.second;
		}
	}
};


size_t IndexOf(const std::vector<std::string> &order, const std::string &path)
{
	for (size_t i = 0; i < order.size(); i++) {
		if (order[i] == path) {
			return i;
		}
	}
	return order.size();
}

}


TEST_CASE("entry statuses")
{
	TempDirectory dir;
	dir.Write("a", "abc");
	dir.Write("b", "abd");
	dir.Write("c", "abc");
	dir.Write("empty1", "");
	dir.Write("empty2", "");
	dir.MakeDirectory("d");
	REQUIRE(symlink("a", (dir / "link1").c_str()) == 0);
	REQUIRE(symlink("b", (dir / "link2").c_str()) == 0);
	REQUIRE(symlink("a", (dir / "link3").c_str()) == 0);

	const EntryInfo none;
	const EntryInfo a = ReadEntryInfo(dir / "a");
	CHECK(a.type == EntryType::File);
	CHECK(a.size == 3);
	CHECK(ReadEntryInfo(dir / "missing").type == EntryType::None);
	CHECK(ReadEntryInfo(dir / "link1").linkTarget == "a");

	CHECK(InitialStatus(a, none) == EntryStatus::LeftOnly);
	CHECK(InitialStatus(none, a) == EntryStatus::RightOnly);
	CHECK(InitialStatus(a, ReadEntryInfo(dir / "b")) == EntryStatus::Pending);
	CHECK(InitialStatus(a, ReadEntryInfo(dir / "empty1")) == EntryStatus::Different);
	CHECK(InitialStatus(ReadEntryInfo(dir / "empty1"), ReadEntryInfo(dir / "empty2")) == EntryStatus::Same);
	CHECK(InitialStatus(a, ReadEntryInfo(dir / "d")) == EntryStatus::TypeMismatch);
	CHECK(InitialStatus(ReadEntryInfo(dir / "d"), ReadEntryInfo(dir / "d")) == EntryStatus::Same);
	CHECK(InitialStatus(ReadEntryInfo(dir / "link1"), ReadEntryInfo(dir / "link2")) == EntryStatus::Different);
	CHECK(InitialStatus(ReadEntryInfo(dir / "link1"), ReadEntryInfo(dir / "link3")) == EntryStatus::Same);
	CHECK(InitialStatus(a, ReadEntryInfo(dir / "link1")) == EntryStatus::TypeMismatch);

	CHECK(CompareFiles(dir / "a", dir / "c") == EntryStatus::Same);
	CHECK(CompareFiles(dir / "a", dir / "b") == EntryStatus::Different);
	CHECK(CompareFiles(dir / "a", dir / "missing") == EntryStatus::Error);

	// Larger than one block, differing at the end.
	std::string big(200000, 'x');
	dir.Write("big1", big);
	big.back() = 'y';
	dir.Write("big2", big);
	CHECK(CompareFiles(dir / "big1", dir / "big2") == EntryStatus::Different);
	CHECK(CompareFiles(dir / "big1", dir / "big1") == EntryStatus::Same);
	const std::atomic<bool> cancelled(true);
	CHECK(CompareFiles(dir / "big1", dir / "big1", &cancelled) == EntryStatus::Pending);

	using Statuses = std::vector<EntryStatus>;
	CHECK(AggregateStatus(EntryStatus::Same, {}) == EntryStatus::Same);
	CHECK(AggregateStatus(EntryStatus::Same, Statuses {EntryStatus::Same, EntryStatus::Pending}) == EntryStatus::Pending);
	CHECK(AggregateStatus(EntryStatus::Same, Statuses {EntryStatus::Pending, EntryStatus::LeftOnly})
		== EntryStatus::Different);
	CHECK(AggregateStatus(EntryStatus::Error, Statuses {EntryStatus::Same}) == EntryStatus::Error);
}


TEST_CASE("scanning two trees")
{
	TempDirectory left;
	TempDirectory right;
	left.Write("same.txt", "same\n");
	right.Write("same.txt", "same\n");
	left.Write("changed.txt", "abc\n");
	right.Write("changed.txt", "abd\n");
	left.Write("longer.txt", "abc\n");
	right.Write("longer.txt", "abcd\n");
	left.Write("left.txt", "l\n");
	right.Write("right.txt", "r\n");
	left.Write("sub/inner.txt", "inner\n");
	right.Write("sub/inner.txt", "inner\n");
	left.Write("onlyleft/deep/file.txt", "x\n");
	left.Write("kind", "file\n");
	right.MakeDirectory("kind");
	right.Write("kind/child.txt", "c\n");
	left.Write(".git/config", "a\n");
	right.Write(".git/config", "b\n");

	CollectingSink sink;
	const std::atomic<bool> cancel(false);
	REQUIRE(Scan(left.Path(), right.Path(), ScanOptions(), sink, cancel));
	std::map<std::string, EntryStatus> &statuses = sink.statuses;
	CHECK(statuses["same.txt"] == EntryStatus::Same);
	CHECK(statuses["changed.txt"] == EntryStatus::Different);
	CHECK(statuses["longer.txt"] == EntryStatus::Different);
	CHECK(statuses["left.txt"] == EntryStatus::LeftOnly);
	CHECK(statuses["right.txt"] == EntryStatus::RightOnly);
	CHECK(statuses["sub"] == EntryStatus::Same);
	CHECK(statuses["sub/inner.txt"] == EntryStatus::Same);
	CHECK(statuses["onlyleft"] == EntryStatus::LeftOnly);
	CHECK(statuses["onlyleft/deep/file.txt"] == EntryStatus::LeftOnly);
	CHECK(statuses["kind"] == EntryStatus::TypeMismatch);
	CHECK(statuses["kind/child.txt"] == EntryStatus::RightOnly);
	CHECK(statuses.count(".git") == 0);
	CHECK(statuses.count(".git/config") == 0);
	CHECK(IndexOf(sink.order, "onlyleft") < IndexOf(sink.order, "onlyleft/deep"));
	CHECK(IndexOf(sink.order, "onlyleft/deep") < IndexOf(sink.order, "onlyleft/deep/file.txt"));

	ScanOptions withVcs;
	withVcs.ignoreVcs = false;
	CollectingSink vcsSink;
	REQUIRE(Scan(left.Path(), right.Path(), withVcs, vcsSink, cancel));
	CHECK(vcsSink.statuses[".git/config"] == EntryStatus::Different);

	CollectingSink cancelledSink;
	const std::atomic<bool> cancelled(true);
	CHECK_FALSE(Scan(left.Path(), right.Path(), ScanOptions(), cancelledSink, cancelled));

	CHECK_THROWS(Scan(left / "missing", right.Path(), ScanOptions(), sink, cancel));
}


} // DirCompare
