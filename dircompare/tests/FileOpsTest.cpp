#include <fcntl.h>
#include <fs_attr.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <TypeConstants.h>
#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "Compare.h"
#include "FileOps.h"
#include "FilePair.h"
#include "TempDirectory.h"


namespace DirCompare {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


static void WriteAttribute(const std::string &path, const char *name, const std::string &value)
{
	const int fd = open(path.c_str(), O_RDONLY);
	REQUIRE(fd >= 0);
	CHECK(fs_write_attr(fd, name, B_STRING_TYPE, 0, value.c_str(), value.size() + 1)
		== static_cast<ssize_t>(value.size() + 1));
	close(fd);
}


static std::string ReadAttribute(const std::string &path, const char *name)
{
	const int fd = open(path.c_str(), O_RDONLY);
	REQUIRE(fd >= 0);
	char buffer[256] {};
	const ssize_t size = fs_read_attr(fd, name, B_STRING_TYPE, 0, buffer, sizeof(buffer) - 1);
	close(fd);
	return size > 0 ? std::string(buffer) : std::string();
}


static std::string Text(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


TEST_CASE("copying and deleting entries")
{
	TempDirectory source;
	TempDirectory target;
	std::vector<std::string> errors;

	// A file keeps its data, permissions, time and attributes.
	source.Write("tool.sh", "#!/bin/sh\n");
	REQUIRE(chmod((source / "tool.sh").c_str(), 0751) == 0);
	const timespec times[2] = {{1000000, 0}, {2000000, 0}};
	REQUIRE(utimensat(AT_FDCWD, (source / "tool.sh").c_str(), times, 0) == 0);
	WriteAttribute(source / "tool.sh", "test:note", "hello");
	REQUIRE(CopyEntry(source / "tool.sh", target / "nested/tool.sh", errors));
	CHECK(errors.empty());
	CHECK(target.Read("nested/tool.sh") == "#!/bin/sh\n");
	const EntryInfo copied = ReadEntryInfo(target / "nested/tool.sh");
	CHECK((copied.mode & 07777) == 0751);
	CHECK(copied.modified.tv_sec == 2000000);
	CHECK(ReadAttribute(target / "nested/tool.sh", "test:note") == "hello");

	// A file replaces another; the copy is equal.
	target.Write("nested/tool.sh", "old, longer contents\n");
	REQUIRE(CopyEntry(source / "tool.sh", target / "nested/tool.sh", errors));
	CHECK(CompareFiles(source / "tool.sh", target / "nested/tool.sh") == EntryStatus::Same);

	// A directory merges: entries are replaced, extra target entries stay.
	source.Write("dir/a.txt", "new a\n");
	source.Write("dir/sub/b.txt", "b\n");
	REQUIRE(symlink("a.txt", (source / "dir/link").c_str()) == 0);
	target.Write("dir/a.txt", "old a\n");
	target.Write("dir/extra.txt", "extra\n");
	REQUIRE(CopyEntry(source / "dir", target / "dir", errors));
	CHECK(errors.empty());
	CHECK(target.Read("dir/a.txt") == "new a\n");
	CHECK(target.Read("dir/sub/b.txt") == "b\n");
	CHECK(target.Read("dir/extra.txt") == "extra\n");
	CHECK(ReadEntryInfo(target / "dir/link").linkTarget == "a.txt");

	// Another type is replaced.
	source.Write("kind", "file\n");
	target.Write("kind/child.txt", "c\n");
	REQUIRE(CopyEntry(source / "kind", target / "kind", errors));
	CHECK(ReadEntryInfo(target / "kind").type == EntryType::File);

	REQUIRE(DeleteEntry(target / "dir", errors));
	CHECK(ReadEntryInfo(target / "dir").type == EntryType::None);
	REQUIRE(DeleteEntry(target / "kind", errors));
	CHECK(ReadEntryInfo(target / "kind").type == EntryType::None);
	CHECK(errors.empty());

	CHECK_FALSE(CopyEntry(source / "missing", target / "missing", errors));
	CHECK(errors.size() == 1);
}


TEST_CASE("file pairs")
{
	TempDirectory left;
	TempDirectory right;
	left.Write("a.txt", "1\r\n2\r\n");
	right.Write("a.txt", "1\r\nx\r\n");

	BReference<FilePair> pair(new FilePair(left / "a.txt", right / "a.txt"), true);
	pair->Load();
	CHECK(pair->Exists(0));
	CHECK(pair->Problem(0).empty());
	CHECK(Text(*pair->Model(0)) == "1\n2\n");
	CHECK(Text(*pair->Model(1)) == "1\nx\n");

	// Saving keeps the line endings and the attributes.
	WriteAttribute(left / "a.txt", "test:note", "kept");
	{
		ModelLock lock(*pair->Model(0));
		pair->Model(0)->Insert(0, "0\n");
	}
	CHECK(pair->IsModified(0));
	pair->Save(0);
	CHECK_FALSE(pair->IsModified(0));
	CHECK(left.Read("a.txt") == "0\r\n1\r\n2\r\n");
	CHECK(ReadAttribute(left / "a.txt", "test:note") == "kept");

	// Edits survive a change on disk unless forced.
	{
		ModelLock lock(*pair->Model(1));
		pair->Model(1)->Insert(0, "edit\n");
	}
	right.Write("a.txt", "changed\n");
	pair->Load();
	CHECK(pair->ChangedOnDisk(1));
	CHECK(Text(*pair->Model(1)) == "edit\n1\nx\n");
	pair->Load(true);
	CHECK(Text(*pair->Model(1)) == "changed\n");

	// A missing side is created with its directories.
	BReference<FilePair> fresh(new FilePair(left / "a.txt", right / "new/dir/a.txt"), true);
	fresh->Load();
	CHECK_FALSE(fresh->Exists(1));
	CHECK(fresh->Problem(1).empty());
	{
		ModelLock lock(*fresh->Model(1));
		fresh->Model(1)->Insert(0, "created\n");
	}
	fresh->Save(1);
	CHECK(fresh->Exists(1));
	CHECK(right.Read("new/dir/a.txt") == "created\n");

	// Sides that are not text.
	left.Write("binary", std::string("a\0b", 3));
	left.MakeDirectory("directory");
	REQUIRE(symlink("a.txt", (left / "link").c_str()) == 0);
	BReference<FilePair> other(new FilePair(left / "binary", left / "directory"), true);
	other->Load();
	CHECK(other->Problem(0) == "is a binary file");
	CHECK(other->Problem(1) == "is a directory");
	BReference<FilePair> link(new FilePair(left / "link", left / "a.txt"), true);
	link->Load();
	CHECK(link->Problem(0) == "is a symbolic link");
	CHECK(Text(*link->Model(0)).empty());
}


} // DirCompare
