#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "Chunks.h"
#include "FileDocument.h"
#include "Git.h"
#include "Repository.h"


namespace GitDiff {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


GitLibrary sGitLibrary;


// A repository in a temporary directory, built with libgit2 only.
class TestRepository {
private:
	std::string fDirectory;
	RepositoryPtr fRepository;

public:
	TestRepository()
	{
		char directory[] = "/tmp/GitDiffTest-XXXXXX";
		REQUIRE(mkdtemp(directory) != nullptr);
		fDirectory = directory;
		git_repository *repository = nullptr;
		Check(git_repository_init(&repository, fDirectory.c_str(), false), "init");
		fRepository.reset(repository);
	}

	~TestRepository()
	{
		fRepository.reset();
		std::filesystem::remove_all(fDirectory);
	}

	const std::string &Directory() const {return fDirectory;}

	void Write(const std::string &path, std::string_view data)
	{
		std::ofstream file(fDirectory + "/" + path, std::ios::binary | std::ios::trunc);
		file.write(data.data(), data.size());
	}

	void Remove(const std::string &path)
	{
		std::filesystem::remove(fDirectory + "/" + path);
	}

	void Add(const std::string &path)
	{
		git_index *index = nullptr;
		Check(git_repository_index(&index, fRepository.get()), "index");
		IndexPtr indexPtr(index);
		Check(git_index_read(index, false), "read");
		Check(git_index_add_bypath(index, path.c_str()), "add");
		Check(git_index_write(index), "write");
	}

	void Commit()
	{
		git_index *index = nullptr;
		Check(git_repository_index(&index, fRepository.get()), "index");
		IndexPtr indexPtr(index);
		Check(git_index_read(index, false), "read");
		git_oid treeId;
		Check(git_index_write_tree(&treeId, index), "write tree");
		git_tree *tree = nullptr;
		Check(git_tree_lookup(&tree, fRepository.get(), &treeId), "tree");
		git_signature *signature = nullptr;
		Check(git_signature_new(&signature, "Test", "test@example.com", 0, 0), "signature");

		git_oid parentId;
		git_commit *parent = nullptr;
		if (git_reference_name_to_id(&parentId, fRepository.get(), "HEAD") == 0) {
			Check(git_commit_lookup(&parent, fRepository.get(), &parentId), "parent");
		}
		const git_commit *parents[] = {parent};
		git_oid commitId;
		Check(git_commit_create(&commitId, fRepository.get(), "HEAD", signature, signature, nullptr, "test",
			tree, parent != nullptr ? 1 : 0, parents), "commit");
		git_commit_free(parent);
		git_signature_free(signature);
		git_tree_free(tree);
	}

	// Adds blobs for the conflict stages of a path.
	void AddConflict(const std::string &path)
	{
		git_index *index = nullptr;
		Check(git_repository_index(&index, fRepository.get()), "index");
		IndexPtr indexPtr(index);
		Check(git_index_read(index, false), "read");
		git_index_entry entries[3] {};
		const char *contents[] = {"base\n", "ours\n", "theirs\n"};
		for (int i = 0; i < 3; i++) {
			Check(git_blob_create_from_buffer(&entries[i].id, fRepository.get(), contents[i], strlen(contents[i])), "blob");
			entries[i].path = path.c_str();
			entries[i].mode = GIT_FILEMODE_BLOB;
		}
		Check(git_index_conflict_add(index, &entries[0], &entries[1], &entries[2]), "conflict");
		Check(git_index_write(index), "write");
	}
};


static std::string Text(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


static const StatusEntry *FindEntry(const std::vector<StatusEntry> &entries, const std::string &path)
{
	for (const StatusEntry &entry : entries) {
		if (entry.path == path) {
			return &entry;
		}
	}
	return nullptr;
}


TEST_CASE("repository status and versions")
{
	TestRepository test;
	test.Write("a.txt", "one\ntwo\n");
	test.Add("a.txt");
	test.Commit();
	test.Write("a.txt", "one\ntwo\nthree\n");
	test.Write("b.txt", "new\n");
	test.Add("b.txt");
	test.Write("c.txt", "untracked\n");

	Repository repository(test.Directory());
	CHECK(repository.RelativePath(repository.Workdir() + "dir/x.c") == "dir/x.c");
	CHECK_FALSE(repository.RelativePath("/elsewhere/x.c").has_value());

	const std::vector<StatusEntry> status = repository.Status();
	REQUIRE(status.size() == 3);
	CHECK(*FindEntry(status, "a.txt") == StatusEntry {"a.txt", ' ', 'M', false});
	CHECK(*FindEntry(status, "b.txt") == StatusEntry {"b.txt", 'A', ' ', false});
	CHECK(*FindEntry(status, "c.txt") == StatusEntry {"c.txt", ' ', '?', false});

	CHECK(repository.ReadHead("a.txt")->data == "one\ntwo\n");
	CHECK(repository.ReadIndex("a.txt")->data == "one\ntwo\n");
	CHECK(repository.ReadWorktree("a.txt")->data == "one\ntwo\nthree\n");
	CHECK_FALSE(repository.ReadHead("b.txt").has_value());
	CHECK(repository.ReadIndex("b.txt")->data == "new\n");
	CHECK_FALSE(repository.ReadIndex("c.txt").has_value());
	CHECK_FALSE(repository.ReadWorktree("missing.txt").has_value());
	CHECK_FALSE(repository.IsConflicted("a.txt"));

	// The written blob is seen by a fresh repository object, and the
	// working tree file still differs from it.
	repository.WriteIndex("a.txt", "one\nTWO\n", GIT_FILEMODE_BLOB);
	Repository other(test.Directory());
	CHECK(other.ReadIndex("a.txt")->data == "one\nTWO\n");
	CHECK(*FindEntry(other.Status(), "a.txt") == StatusEntry {"a.txt", 'M', 'M', false});

	// Staging the working tree content leaves nothing unstaged.
	repository.WriteIndex("a.txt", "one\ntwo\nthree\n", GIT_FILEMODE_BLOB);
	CHECK(*FindEntry(repository.Status(), "a.txt") == StatusEntry {"a.txt", 'M', ' ', false});

	repository.RemoveFromIndex("b.txt");
	CHECK(*FindEntry(repository.Status(), "b.txt") == StatusEntry {"b.txt", ' ', '?', false});
}


TEST_CASE("file documents stage and unstage")
{
	TestRepository test;
	test.Write("a.txt", "1\n2\n3\n");
	test.Add("a.txt");
	test.Commit();
	test.Write("a.txt", "1\nx\n3\n4\n");
	test.Write("new.txt", "fresh\n");

	Repository repository(test.Directory());
	BReference<FileDocument> document(new FileDocument("a.txt"), true);
	document->Load(repository);
	CHECK(document->ReadOnlyReason().empty());
	CHECK(Text(*document->Model(Side::Head)) == "1\n2\n3\n");
	CHECK(Text(*document->Model(Side::Index)) == "1\n2\n3\n");
	CHECK(Text(*document->Model(Side::Worktree)) == "1\nx\n3\n4\n");

	// Stage the second chunk only.
	TextModel &index = *document->Model(Side::Index);
	TextModel &worktree = *document->Model(Side::Worktree);
	const std::vector<Chunk> chunks = ComputeChunks(Text(index), Text(worktree));
	REQUIRE(chunks.size() == 2);
	ApplyToA(index, worktree, chunks, {1});
	CHECK(document->IsModified(Side::Index));
	document->WriteIndex(repository);
	CHECK_FALSE(document->IsModified(Side::Index));
	CHECK(repository.ReadIndex("a.txt")->data == "1\n2\n3\n4\n");

	document->StageFile(repository);
	CHECK(repository.ReadIndex("a.txt")->data == "1\nx\n3\n4\n");
	document->UnstageFile(repository);
	CHECK(repository.ReadIndex("a.txt")->data == "1\n2\n3\n");

	// Edits survive a reload unless forced.
	{
		ModelLock lock(worktree);
		worktree.Insert(0, "0\n");
	}
	test.Write("a.txt", "changed\n");
	document->Load(repository);
	CHECK(Text(worktree) == "0\n1\nx\n3\n4\n");
	CHECK(document->ChangedOnDisk(Side::Worktree));
	document->Load(repository, true);
	CHECK(Text(worktree) == "changed\n");
	CHECK_FALSE(document->ChangedOnDisk(Side::Worktree));

	// A new file is added to the index and removed again.
	BReference<FileDocument> fresh(new FileDocument("new.txt"), true);
	fresh->Load(repository);
	CHECK_FALSE(fresh->Exists(Side::Index));
	fresh->StageFile(repository);
	CHECK(repository.ReadIndex("new.txt")->data == "fresh\n");
	CHECK(fresh->Exists(Side::Index));
	fresh->UnstageFile(repository);
	CHECK_FALSE(repository.ReadIndex("new.txt").has_value());
	CHECK_FALSE(fresh->Exists(Side::Index));

	// Staging a deleted file removes it from the index.
	test.Remove("a.txt");
	document->Load(repository, true);
	CHECK_FALSE(document->Exists(Side::Worktree));
	document->StageFile(repository);
	CHECK(*FindEntry(repository.Status(), "a.txt") == StatusEntry {"a.txt", 'D', ' ', false});
}


TEST_CASE("line endings and read-only files")
{
	TestRepository test;
	test.Write("crlf.txt", "x\r\ny\r\n");
	test.Write("mixed.txt", "x\r\ny\n");
	test.Write("binary.dat", std::string_view("a\0b\n", 4));
	test.Add("crlf.txt");
	test.Add("mixed.txt");
	test.Add("binary.dat");
	test.Commit();
	Repository repository(test.Directory());

	// Edited as LF, written back as CRLF.
	BReference<FileDocument> crlf(new FileDocument("crlf.txt"), true);
	crlf->Load(repository);
	CHECK(crlf->ReadOnlyReason().empty());
	TextModel &index = *crlf->Model(Side::Index);
	CHECK(Text(index) == "x\ny\n");
	{
		ModelLock lock(index);
		index.Insert(index.Text().Length(), "z\n");
	}
	crlf->WriteIndex(repository);
	CHECK(repository.ReadIndex("crlf.txt")->data == "x\r\ny\r\nz\r\n");

	BReference<FileDocument> mixed(new FileDocument("mixed.txt"), true);
	mixed->Load(repository);
	CHECK(mixed->ReadOnlyReason() == "has mixed line endings");

	BReference<FileDocument> binary(new FileDocument("binary.dat"), true);
	binary->Load(repository);
	CHECK(binary->ReadOnlyReason() == "is a binary file");

	test.AddConflict("conflict.txt");
	test.Write("conflict.txt", "<<<<<<<\n");
	CHECK(repository.IsConflicted("conflict.txt"));
	CHECK(FindEntry(repository.Status(), "conflict.txt")->conflicted);
	BReference<FileDocument> conflict(new FileDocument("conflict.txt"), true);
	conflict->Load(repository);
	CHECK(conflict->ReadOnlyReason() == "has merge conflicts");
}


static git_oid ParentOf(git_repository *repository, const git_oid &id)
{
	git_commit *commit = nullptr;
	Check(git_commit_lookup(&commit, repository, &id), "lookup");
	CommitPtr commitPtr(commit);
	REQUIRE(git_commit_parentcount(commit) == 1);
	return *git_commit_parent_id(commit, 0);
}


TEST_CASE("commit and amend")
{
	TestRepository test;
	test.Write("a.txt", "1\n");
	test.Add("a.txt");
	test.Commit();

	Repository repository(test.Directory());
	git_config *config = nullptr;
	Check(git_repository_config(&config, repository.Get()), "config");
	ConfigPtr configPtr(config);
	Check(git_config_set_string(config, "user.name", "Committer"), "name");
	Check(git_config_set_string(config, "user.email", "committer@example.com"), "email");

	CHECK_THROWS_AS(repository.Commit("Nothing", false), GitError);
	repository.WriteIndex("a.txt", "2\n", GIT_FILEMODE_BLOB);
	CHECK_THROWS_AS(repository.Commit("  \n", false), GitError);

	const git_oid first = repository.Commit("Change a\n\n  ", false);
	CHECK(repository.HeadMessage() == "Change a\n");
	CHECK(repository.ReadHead("a.txt")->data == "2\n");
	// The file on disk still differs; nothing is staged.
	const std::vector<StatusEntry> status = repository.Status();
	CHECK(*FindEntry(status, "a.txt") == StatusEntry {"a.txt", ' ', 'M', false});

	repository.WriteIndex("a.txt", "3\n", GIT_FILEMODE_BLOB);
	const git_oid amended = repository.Commit("Change a again", true);
	CHECK_FALSE(git_oid_equal(&first, &amended));
	CHECK(repository.HeadMessage() == "Change a again\n");
	CHECK(repository.ReadHead("a.txt")->data == "3\n");
	const git_oid firstParent = ParentOf(repository.Get(), first);
	const git_oid amendedParent = ParentOf(repository.Get(), amended);
	CHECK(git_oid_equal(&firstParent, &amendedParent));

	// Amending with nothing staged only changes the message.
	repository.Commit("Reworded", true);
	CHECK(repository.HeadMessage() == "Reworded\n");
}


TEST_CASE("Change-Id setting")
{
	TestRepository test;
	test.Write("a.txt", "1\n");
	test.Add("a.txt");
	test.Commit();

	Repository repository(test.Directory());
	git_config *config = nullptr;
	Check(git_repository_config(&config, repository.Get()), "config");
	ConfigPtr configPtr(config);
	Check(git_config_set_string(config, "user.name", "Committer"), "name");
	Check(git_config_set_string(config, "user.email", "committer@example.com"), "email");
	CHECK_FALSE(repository.CreatesChangeId());

	// Gerrit's hook turns it on unless the setting says otherwise.
	std::filesystem::create_directories(test.Directory() + "/.git/hooks");
	std::ofstream(test.Directory() + "/.git/hooks/commit-msg") << "#!/bin/sh\n# Change-Id hook\n";
	CHECK(repository.CreatesChangeId());
	repository.SetCreatesChangeId(false);
	CHECK_FALSE(repository.CreatesChangeId());
	repository.SetCreatesChangeId(true);
	CHECK(repository.CreatesChangeId());

	repository.WriteIndex("a.txt", "2\n", GIT_FILEMODE_BLOB);
	repository.Commit("Change a", false);
	const std::string message = repository.HeadMessage();
	REQUIRE(message.starts_with("Change a\n\nChange-Id: I"));
	CHECK(message.size() == std::string("Change a\n\nChange-Id: I").size() + 40 + 1);

	// Amending keeps the Change-Id of the message.
	repository.Commit(message, true);
	CHECK(repository.HeadMessage() == message);

	repository.SetCreatesChangeId(false);
	repository.Commit("Plain", true);
	CHECK(repository.HeadMessage() == "Plain\n");
}


} // GitDiff
