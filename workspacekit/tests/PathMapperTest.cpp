#include <catch2/catch_test_macros.hpp>

#include "PathMapper.h"


namespace Workspace {


static const std::string kOid = "0123456789abcdef0123456789abcdef01234567";


TEST_CASE("relative paths of directories")
{
	CHECK(RelativeTo("/a/b/c", "/a") == "b/c");
	CHECK(RelativeTo("/a/b", "/a/b") == "");
	CHECK(RelativeTo("/a/b", "/") == "a/b");
	CHECK_FALSE(RelativeTo("/ab/c", "/a").has_value());
	CHECK_FALSE(RelativeTo("/a", "/a/b").has_value());
	CHECK(RelativeTo("src/a", "") == "src/a");
	CHECK(RelativeTo("src/a", "src") == "a");
}


TEST_CASE("tool paths map to locators and back")
{
	const Locator remote = Locator::Sftp("", "vm", 0, "/home/user/project");
	const Locator commit = Locator::GitCommit("/src/project", kOid, "");
	PathMapper mapper({
		{"/build/remote/", remote},
		{"/build/old", commit},
	});
	CHECK(mapper.Rules()[0].toolDirectory == "/build/remote");

	CHECK(mapper.ToLocator("/build/remote/src/../lib/a.c") == remote.WithPath("/home/user/project/lib/a.c"));
	CHECK(mapper.ToLocator("/build/old/src/a.c") == commit.WithPath("src/a.c"));
	CHECK(mapper.ToLocator("/build/older/a.c") == Locator::File("/build/older/a.c"));
	CHECK_FALSE(mapper.ToLocator("relative.c").has_value());

	CHECK(mapper.ToToolPath(remote.WithPath("/home/user/project/lib/a.c")) == "/build/remote/lib/a.c");
	CHECK(mapper.ToToolPath(commit.WithPath("src/a.c")) == "/build/old/src/a.c");
	CHECK(mapper.ToToolPath(Locator::File("/boot/home/a.c")) == "/boot/home/a.c");
	// Another commit, server or a path outside the rule's directory.
	CHECK_FALSE(mapper.ToToolPath(Locator::GitCommit("/src/project", "HEAD", "src/a.c")).has_value());
	CHECK_FALSE(mapper.ToToolPath(Locator::Sftp("", "other", 0, "/home/user/project/a.c")).has_value());
	CHECK_FALSE(mapper.ToToolPath(remote.WithPath("/home/user/a.c")).has_value());
	// The tool path of this local file is mapped to the remote file.
	CHECK_FALSE(mapper.ToToolPath(Locator::File("/build/remote/a.c")).has_value());
}


TEST_CASE("local directories map to other local directories")
{
	PathMapper mapper({{"/build", Locator::File("/boot/home/project")}});
	CHECK(mapper.ToLocator("/build/a.c") == Locator::File("/boot/home/project/a.c"));
	CHECK(mapper.ToToolPath(Locator::File("/boot/home/project/a.c")) == "/build/a.c");
	CHECK(mapper.ToToolPath(Locator::File("/boot/home/other.c")) == "/boot/home/other.c");
}


} // Workspace
