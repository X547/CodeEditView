#pragma once

#include <limits.h>
#include <stdlib.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <Autolock.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <OS.h>

#include "Git.h"


namespace Workspace {


// A temporary directory, deleted with its contents at the end. Its path is
// the real one (/tmp is a symbolic link on Haiku), as libgit2 reports it.
class TempDirectory {
private:
	std::string fPath;

public:
	TempDirectory()
	{
		char path[] = "/tmp/WorkspaceKitTest-XXXXXX";
		REQUIRE(mkdtemp(path) != nullptr);
		char real[PATH_MAX];
		REQUIRE(realpath(path, real) != nullptr);
		fPath = real;
	}

	~TempDirectory()
	{
		std::error_code error;
		std::filesystem::remove_all(fPath, error);
	}

	const std::string &Path() const {return fPath;}
	std::string operator/(const std::string &relative) const {return fPath + "/" + relative;}

	void Write(const std::string &relative, std::string_view data) const
	{
		const std::filesystem::path path(*this / relative);
		std::filesystem::create_directories(path.parent_path());
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(data.data(), data.size());
	}

	std::string Read(const std::string &relative) const
	{
		std::ifstream file(*this / relative, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}
};


// A repository in a temporary directory; Commit() commits all files.
class TestRepository: public TempDirectory {
private:
	DiffKit::GitLibrary fLibrary;
	DiffKit::RepositoryPtr fRepository;

public:
	TestRepository()
	{
		git_repository *repository = nullptr;
		DiffKit::Check(git_repository_init(&repository, Path().c_str(), false), "init");
		fRepository.reset(repository);
	}

	// Returns the commit id.
	std::string Commit()
	{
		using DiffKit::Check;
		git_index *index = nullptr;
		Check(git_repository_index(&index, fRepository.get()), "index");
		DiffKit::IndexPtr indexPtr(index);
		Check(git_index_add_all(index, nullptr, 0, nullptr, nullptr), "add");
		Check(git_index_write(index), "write");
		git_oid treeId;
		Check(git_index_write_tree(&treeId, index), "write tree");
		git_tree *tree = nullptr;
		Check(git_tree_lookup(&tree, fRepository.get(), &treeId), "tree");
		DiffKit::TreePtr treePtr(tree);
		git_signature *signature = nullptr;
		Check(git_signature_new(&signature, "Test", "test@example.com", 0, 0), "signature");
		DiffKit::SignaturePtr signaturePtr(signature);

		std::vector<const git_commit*> parents;
		git_oid parentId;
		git_commit *parent = nullptr;
		if (git_reference_name_to_id(&parentId, fRepository.get(), "HEAD") == 0) {
			Check(git_commit_lookup(&parent, fRepository.get(), &parentId), "parent");
			parents.push_back(parent);
		}
		DiffKit::CommitPtr parentPtr(parent);
		git_oid commitId;
		Check(git_commit_create(&commitId, fRepository.get(), "HEAD", signature, signature, nullptr, "commit", tree,
			parents.size(), parents.data()), "commit");
		char buffer[GIT_OID_HEXSZ + 1];
		git_oid_tostr(buffer, sizeof(buffer), &commitId);
		return buffer;
	}
};


// Collects messages sent to it.
class NoticeCollector: public BLooper {
private:
	BLocker fLock;
	std::vector<BMessage> fMessages;
	sem_id fSem;

public:
	NoticeCollector():
		BLooper("NoticeCollector"),
		fSem(create_sem(0, "notices"))
	{
	}

	~NoticeCollector() override
	{
		delete_sem(fSem);
	}

	void MessageReceived(BMessage *message) override
	{
		{
			BAutolock lock(fLock);
			fMessages.push_back(*message);
		}
		release_sem(fSem);
	}

	// The first message with the code not taken yet, waiting up to a second.
	std::optional<BMessage> Take(uint32 what)
	{
		const bigtime_t deadline = system_time() + 1000000;
		for (;;) {
			{
				BAutolock lock(fLock);
				for (auto it = fMessages.begin(); it != fMessages.end(); ++it) {
					if (it->what == what) {
						BMessage message = *it;
						fMessages.erase(it);
						return message;
					}
				}
			}
			if (acquire_sem_etc(fSem, 1, B_ABSOLUTE_TIMEOUT, deadline) == B_TIMED_OUT) {
				return std::nullopt;
			}
		}
	}
};


// Owns a running NoticeCollector and quits it on destruction.
class CollectorHolder {
private:
	NoticeCollector *fCollector;

public:
	CollectorHolder():
		fCollector(new NoticeCollector())
	{
		fCollector->Run();
	}

	~CollectorHolder()
	{
		fCollector->Lock();
		fCollector->Quit();
	}

	NoticeCollector *operator->() const {return fCollector;}
	BMessenger Messenger() const {return BMessenger(fCollector);}
};


} // Workspace
