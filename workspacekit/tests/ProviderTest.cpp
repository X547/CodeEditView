#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FileProvider.h"
#include "GitProvider.h"
#include "TestSupport.h"


namespace Workspace {


TEST_CASE("file options as attribute data")
{
	FileOptions options;
	CHECK(UnflattenFileOptions(FlattenFileOptions(options)) == options);
	options.language = "cpp";
	options.tabWidth = 8;
	options.indentSize = 2;
	options.insertSpaces = true;
	CHECK(UnflattenFileOptions(FlattenFileOptions(options)) == options);
	options.language = "";
	options.wordWrap = true;
	CHECK(UnflattenFileOptions(FlattenFileOptions(options)) == options);

	CHECK(UnflattenBookmarks(FlattenBookmarks({3, 1, 3, 10})) == std::vector<size_t> {1, 3, 10});
	CHECK(UnflattenBookmarks(FlattenBookmarks({})).empty());
	CHECK(UnflattenBookmarks("garbage").empty());

	CHECK_FALSE(UnflattenFileOptions("").has_value());
	CHECK_FALSE(UnflattenFileOptions("not a message").has_value());
	const std::string data = FlattenFileOptions(options);
	CHECK_FALSE(UnflattenFileOptions(std::string_view(data).substr(0, data.size() / 2)).has_value());
}


TEST_CASE("file provider attributes")
{
	class NullSink: public DataSink {
	public:
		void Restart() final {}
		void Append(std::string_view data) final {(void)data;}
	};

	TempDirectory directory;
	FileProvider provider;
	const Locator locator = Locator::File(directory / "a.txt");
	CHECK(provider.HasAttributes(locator));
	const std::string version = provider.Write(locator, "one\n", "", false);

	NullSink sink;
	CHECK_FALSE(provider.Read(locator, sink).options.has_value());

	FileAttributes attributes;
	attributes.mimeType = "text/plain";
	attributes.options.tabWidth = 3;
	const std::string after = provider.WriteAttributes(locator, attributes, version);
	const FileInfo info = provider.Read(locator, sink);
	CHECK(info.mimeType == "text/plain");
	CHECK(info.options == attributes.options);
	// The version to expect is the file's: saving does not conflict.
	CHECK(info.version == after);
	CHECK_NOTHROW(provider.Write(locator, "two\n", after, false));
	CHECK(provider.Read(locator, sink).options == attributes.options);

	// Changed elsewhere before: the known version stays, saving conflicts.
	CHECK(provider.WriteAttributes(locator, attributes, "other") == "other");

	const std::string current = provider.Read(locator, sink).version;
	const std::string afterBookmarks = provider.WriteBookmarks(locator, {0, 4}, current);
	FileInfo marked = provider.Read(locator, sink);
	CHECK(marked.bookmarks == std::vector<size_t> {0, 4});
	CHECK(marked.version == afterBookmarks);
	provider.WriteBookmarks(locator, {}, afterBookmarks);
	CHECK(provider.Read(locator, sink).bookmarks.empty());
	// Removing none is fine too.
	CHECK_NOTHROW(provider.WriteBookmarks(locator, {}, afterBookmarks));
}


TEST_CASE("file provider")
{
	TempDirectory directory;
	FileProvider provider;
	const Locator locator = Locator::File(directory / "a.txt");
	CHECK(provider.IsWritable(locator));
	CHECK(provider.Resolve(locator) == locator);

	FileData missing = provider.Read(locator);
	CHECK_FALSE(missing.data.has_value());
	CHECK(missing.version.empty());

	const std::string version = provider.Write(locator, "one\n", "", false);
	CHECK_FALSE(version.empty());
	const FileData read = provider.Read(locator);
	CHECK(read.data == "one\n");
	CHECK(read.version == version);
	// The file exists now.
	CHECK_THROWS_AS(provider.Write(locator, "x", "", false), VersionConflict);

	directory.Write("a.txt", "changed elsewhere\n");
	CHECK_THROWS_AS(provider.Write(locator, "two\n", version, false), VersionConflict);
	CHECK(directory.Read("a.txt") == "changed elsewhere\n");
	const std::string overwritten = provider.Write(locator, "two\n", version, true);
	CHECK(directory.Read("a.txt") == "two\n");
	CHECK(provider.Write(locator, "three\n", overwritten, false) != overwritten);

	directory.Write("sub/b.txt", "b");
	std::filesystem::create_symlink("sub", directory / "link");
	std::filesystem::create_symlink("none", directory / "loose");
	const std::vector<DirEntry> entries = provider.List(Locator::File(directory.Path()));
	REQUIRE(entries.size() == 4);
	CHECK(entries[0].name == "a.txt");
	CHECK_FALSE(entries[0].directory);
	CHECK_FALSE(entries[0].link);
	CHECK(entries[0].size == 6u);
	CHECK(entries[0].modified.has_value());
	// A link to a directory can be listed.
	CHECK(entries[1].name == "link");
	CHECK(entries[1].directory);
	CHECK(entries[1].link);
	CHECK(entries[2].name == "loose");
	CHECK_FALSE(entries[2].directory);
	CHECK(entries[2].link);
	CHECK_FALSE(entries[2].size.has_value());
	CHECK(entries[3].name == "sub");
	CHECK(entries[3].directory);
	CHECK_FALSE(entries[3].size.has_value());
	CHECK_THROWS_AS(provider.List(Locator::File(directory / "none")), ProviderError);
}


TEST_CASE("file provider writes atomically")
{
	TempDirectory directory;
	FileProvider provider;
	directory.Write("a.txt", "one\n");
	const std::filesystem::path path = directory / "a.txt";
	std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write
		| std::filesystem::perms::group_read);
	const std::filesystem::perms permissions = std::filesystem::status(path).permissions();

	provider.Write(Locator::File(path.string()), "two\n", {}, true);
	CHECK(directory.Read("a.txt") == "two\n");
	CHECK(std::filesystem::status(path).permissions() == permissions);
	// The temporary file is gone.
	std::vector<std::string> names;
	for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(directory.Path())) {
		names.push_back(entry.path().filename().string());
	}
	CHECK(names == std::vector<std::string> {"a.txt"});

	// A link stays a link; its target gets the data.
	std::filesystem::create_symlink("a.txt", directory / "link.txt");
	provider.Write(Locator::File(directory / "link.txt"), "three\n", {}, true);
	CHECK(std::filesystem::is_symlink(directory / "link.txt"));
	CHECK(directory.Read("a.txt") == "three\n");
	// Also one whose target does not exist yet.
	std::filesystem::create_symlink("sub/b.txt", directory / "dangling.txt");
	std::filesystem::create_directory(directory / "sub");
	provider.Write(Locator::File(directory / "dangling.txt"), "b\n", {}, true);
	CHECK(std::filesystem::is_symlink(directory / "dangling.txt"));
	CHECK(directory.Read("sub/b.txt") == "b\n");
}


TEST_CASE("Git provider")
{
	TestRepository repository;
	repository.Write("src/a.cpp", "one\n");
	const std::string first = repository.Commit();
	repository.Write("src/a.cpp", "two\n");
	repository.Write("b.txt", "b\n");
	const std::string second = repository.Commit();

	GitProvider provider;
	const Locator head = Locator::GitCommit(repository / "src", "HEAD", "src/a.cpp");
	CHECK_FALSE(provider.IsWritable(head));

	const Locator resolved = provider.Resolve(head);
	CHECK(resolved == Locator::GitCommit(repository.Path(), second, "src/a.cpp"));
	CHECK(provider.Resolve(head.WithPath("x").Parent()).Revision() == second);
	CHECK_THROWS_AS(provider.Resolve(Locator::GitCommit(repository.Path(), "nothing", "a")), ProviderError);
	CHECK_THROWS_AS(provider.Resolve(Locator::GitCommit(repository / "../none", "HEAD", "a")), ProviderError);

	const FileData current = provider.Read(resolved);
	CHECK(current.data == "two\n");
	CHECK_FALSE(current.version.empty());
	const FileData old = provider.Read(Locator::GitCommit(repository.Path(), "HEAD~1", "src/a.cpp"));
	CHECK(old.data == "one\n");
	CHECK(old.version != current.version);
	CHECK(provider.Read(Locator::GitCommit(repository.Path(), first, "src/a.cpp")).data == "one\n");
	CHECK_FALSE(provider.Read(resolved.WithPath("none.cpp")).data.has_value());
	CHECK_THROWS_AS(provider.Read(resolved.WithPath("src")), ProviderError);
	CHECK_THROWS_AS(provider.Read(resolved.WithPath("")), ProviderError);
	CHECK_THROWS_AS(provider.Write(resolved, "x", current.version, true), ProviderError);

	std::vector<DirEntry> entries = provider.List(resolved.WithPath(""));
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].name == "b.txt");
	CHECK(entries[1].name == "src");
	CHECK(entries[1].directory);
	entries = provider.List(Locator::GitCommit(repository.Path(), first, "src"));
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].name == "a.cpp");
	CHECK_THROWS_AS(provider.List(resolved), ProviderError);
}


TEST_CASE("Git provider lists sizes and links")
{
	TestRepository repository;
	repository.Write("a.txt", "abc\n");
	repository.Write("dir/b.txt", "");
	std::filesystem::create_symlink("a.txt", repository / "link");
	const std::string commit = repository.Commit();

	GitProvider provider;
	const std::vector<DirEntry> entries = provider.List(Locator::GitCommit(repository.Path(), commit, ""));
	REQUIRE(entries.size() == 3);
	CHECK(entries[0].name == "a.txt");
	CHECK(entries[0].size == 4u);
	CHECK_FALSE(entries[0].link);
	// No times or types in a tree.
	CHECK_FALSE(entries[0].modified.has_value());
	CHECK(entries[0].mimeType.empty());
	CHECK(entries[1].name == "dir");
	CHECK(entries[1].directory);
	CHECK_FALSE(entries[1].size.has_value());
	CHECK(entries[2].name == "link");
	CHECK(entries[2].link);
	CHECK_FALSE(entries[2].directory);
	CHECK(provider.List(Locator::GitCommit(repository.Path(), commit, "dir"))[0].size == 0u);
}


TEST_CASE("providers by kind")
{
	Providers &providers = Providers::Default();
	CHECK(providers.Find(LocatorKind::File) != nullptr);
	CHECK(providers.Find(LocatorKind::GitCommit) != nullptr);
	CHECK(providers.Find(LocatorKind::Sftp) == nullptr);
	try {
		providers.Get(Locator::Sftp("", "host", 0, "/a"));
		FAIL("no error");
	} catch (const ProviderError &error) {
		CHECK(std::string(error.what()) == "SFTP is not supported yet");
	}
}


} // Workspace
