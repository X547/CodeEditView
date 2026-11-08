#include <cctype>
#include <map>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "Locator.h"


namespace Workspace {


static const std::string kOid = "0123456789abcdef0123456789abcdef01234567";


TEST_CASE("paths are normalized lexically")
{
	CHECK(NormalizePath("/a/./b//c/../d/") == "/a/b/d");
	CHECK(NormalizePath("/../a") == "/a");
	CHECK(NormalizePath("/") == "/");
	CHECK(NormalizePath("a/../..") == "..");
	CHECK(NormalizePath("./a/") == "a");
	CHECK(NormalizePath(".") == "");
}


TEST_CASE("percent encoding")
{
	CHECK(PercentEncode("/a b/c#d%e?f") == "/a%20b/c%23d%25e%3Ff");
	CHECK(PercentEncode("a&b=c", "&=") == "a%26b%3Dc");
	CHECK(PercentEncode("\xc3\xa4") == "%C3%A4");
	CHECK(PercentDecode("%C3%a4%20x") == "\xc3\xa4 x");
	CHECK_FALSE(PercentDecode("%2").has_value());
	CHECK_FALSE(PercentDecode("%zz").has_value());
}


TEST_CASE("file locators")
{
	const Locator locator = Locator::File("/boot/home/./src/../a b.cpp");
	CHECK(locator.Kind() == LocatorKind::File);
	CHECK(locator.Path() == "/boot/home/a b.cpp");
	CHECK(locator.ToString() == "file:///boot/home/a%20b.cpp");
	CHECK(Locator::Parse(locator.ToString()) == locator);
	CHECK(locator.IsResolved());
	CHECK(locator.Leaf() == "a b.cpp");
	CHECK(locator.Parent() == Locator::File("/boot/home"));
	CHECK(locator.Sibling("b.h") == Locator::File("/boot/home/b.h"));
	CHECK(locator.DisplayName() == "/boot/home/a b.cpp");
	CHECK(Locator::File("/a").Parent().Path() == "/");
	CHECK(Locator::File("/").Parent().Path() == "/");
	CHECK(Locator::File("/").Leaf().empty());
}


TEST_CASE("Git commit locators")
{
	std::string upper = kOid;
	for (char &ch : upper) {
		ch = static_cast<char>(toupper(ch));
	}
	const Locator locator = Locator::GitCommit("/src/repo/", upper, "/lib/./a&b.cpp");
	CHECK(locator.Kind() == LocatorKind::GitCommit);
	CHECK(locator.Repository() == "/src/repo");
	CHECK(locator.Revision() == kOid);
	CHECK(locator.Path() == "lib/a&b.cpp");
	CHECK(locator.IsResolved());
	CHECK(locator.ToString() == "git:///src/repo?commit=" + kOid + "&path=lib/a%26b.cpp");
	CHECK(Locator::Parse(locator.ToString()) == locator);
	CHECK(locator.DisplayName() == "a&b.cpp \xe2\x80\x94 repo @ 0123456");
	CHECK(locator.Parent().Path() == "lib");
	CHECK(locator.Parent().Parent().Path().empty());
	CHECK(locator.Parent().Parent().DisplayName() == "repo @ 0123456");
	CHECK(locator.Sibling("c.h").Revision() == kOid);

	const Locator head = Locator::GitCommit("/src/repo", "HEAD", "a.cpp");
	CHECK_FALSE(head.IsResolved());
	CHECK(head.DisplayName() == "a.cpp \xe2\x80\x94 repo @ HEAD");
	CHECK(Locator::Parse("git:///src/repo?commit=main~2&path=") == Locator::GitCommit("/src/repo", "main~2", ""));
}


TEST_CASE("SFTP locators")
{
	const Locator locator = Locator::Sftp("me", "server", 2222, "home/me/x.c");
	CHECK(locator.Path() == "/home/me/x.c");
	CHECK(locator.ToString() == "sftp://me@server:2222/home/me/x.c");
	CHECK(Locator::Parse(locator.ToString()) == locator);
	CHECK(locator.DisplayName() == "x.c \xe2\x80\x94 me@server:/home/me");

	const std::optional<Locator> bare = Locator::Parse("sftp://server");
	REQUIRE(bare.has_value());
	CHECK(bare->Path() == "/");
	CHECK(bare->Port() == 0);
	CHECK(bare->User().empty());
}


TEST_CASE("malformed locators")
{
	for (const char *text : {
		"http://example.com/a",
		"/boot/home/a",
		"file://relative",
		"file:///a?b",
		"file:///a#b",
		"file:///a%zz",
		"git:///repo",
		"git:///repo?path=a",
		"git:///repo?commit=&path=a",
		"git:///repo?commit=x&other=1",
		"git://repo?commit=x",
		"sftp://server:0/a",
		"sftp://server:x/a",
		"sftp://@server/a",
		"sftp:///a",
	}) {
		INFO(text);
		CHECK_FALSE(Locator::Parse(text).has_value());
	}
}


TEST_CASE("locators as keys")
{
	std::map<Locator, int> map;
	map[Locator::File("/a")] = 1;
	map[Locator::File("/b/../a")] = 2;
	map[Locator::GitCommit("/a", kOid, "a")] = 3;
	map[Locator::Sftp("", "h", 0, "/a")] = 4;
	CHECK(map.size() == 3);
	CHECK(map[Locator::File("/a")] == 2);
}


} // Workspace
