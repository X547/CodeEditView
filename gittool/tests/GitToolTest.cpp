#include <stdlib.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "CommitDiff.h"
#include "Network.h"
#include "Operations.h"
#include "Plan.h"
#include "Replay.h"
#include "Snapshot.h"


namespace GitTool {


GitLibrary sGitLibrary;


static std::string MakeTemporaryDirectory()
{
	char directory[] = "/tmp/GitToolTest-XXXXXX";
	REQUIRE(mkdtemp(directory) != nullptr);
	// libgit2 records worktree paths with symbolic links resolved.
	return std::filesystem::canonical(directory).string();
}


static void WriteFile(const std::string &path, std::string_view data)
{
	std::filesystem::create_directories(std::filesystem::path(path).parent_path());
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(data.data(), data.size());
}


static std::string ReadFile(const std::string &path)
{
	std::ifstream file(path, std::ios::binary);
	std::stringstream data;
	data << file.rdbuf();
	return data.str();
}


static bool Exists(const std::string &path)
{
	return access(path.c_str(), F_OK) == 0;
}


// Stages everything and commits on HEAD of any repository or worktree.
static git_oid CommitAll(git_repository *repository, const char *message)
{
	git_index *index = nullptr;
	Check(git_repository_index(&index, repository), "index");
	IndexPtr indexPtr(index);
	Check(git_index_read(index, false), "read");
	Check(git_index_add_all(index, nullptr, 0, nullptr, nullptr), "add");
	Check(git_index_update_all(index, nullptr, nullptr, nullptr), "update");
	Check(git_index_write(index), "write");
	git_oid treeId;
	Check(git_index_write_tree(&treeId, index), "write tree");
	git_tree *tree = nullptr;
	Check(git_tree_lookup(&tree, repository, &treeId), "tree");
	TreePtr treePtr(tree);
	git_signature *signature = nullptr;
	Check(git_signature_now(&signature, "Test", "test@example.com"), "signature");
	SignaturePtr signaturePtr(signature);
	git_oid parentId;
	git_commit *parent = nullptr;
	if (git_reference_name_to_id(&parentId, repository, "HEAD") == 0) {
		Check(git_commit_lookup(&parent, repository, &parentId), "parent");
	}
	CommitPtr parentPtr(parent);
	const git_commit *parents[] = {parent};
	git_oid commitId;
	Check(git_commit_create(&commitId, repository, "HEAD", signature, signature, nullptr, message, tree,
		parent != nullptr ? 1 : 0, parents), "commit");
	return commitId;
}


static void Stage(git_repository *repository, const char *path)
{
	git_index *index = nullptr;
	Check(git_repository_index(&index, repository), "index");
	IndexPtr indexPtr(index);
	Check(git_index_read(index, false), "read");
	Check(git_index_add_bypath(index, path), "add");
	Check(git_index_write(index), "write");
}


static git_oid RefId(git_repository *repository, const std::string &name)
{
	git_oid id;
	Check(git_reference_name_to_id(&id, repository, name.c_str()), "ref");
	return id;
}


static bool HasRef(git_repository *repository, const std::string &name)
{
	git_oid id;
	const bool found = git_reference_name_to_id(&id, repository, name.c_str()) == 0;
	git_error_clear();
	return found;
}


static void MoveRef(git_repository *repository, const char *name, const git_oid &id)
{
	git_reference *reference = nullptr;
	Check(git_reference_create(&reference, repository, name, &id, 1, "test"), "move");
	git_reference_free(reference);
}


static void Run(const Plan &plan, git_repository *repository, Resolution resolution = Resolution::Cancel,
	NetworkHandler *network = nullptr)
{
	INFO(plan.Describe(repository));
	REQUIRE(plan.blocked.empty());
	RunContext context;
	context.repository = repository;
	context.resolution = resolution;
	context.network = network;
	plan.run(context);
}


class TestHandler: public NetworkHandler {
public:
	bool IsCancelled() override {return false;}
	void Progress(const std::string &text) override {(void)text;}
	void Log(const std::string &text) override {(void)text;}

	bool AskPassword(const std::string &url, std::string &user, std::string &password) override
	{
		(void)url;
		(void)user;
		(void)password;
		return false;
	}

	bool ConfirmServer(const std::string &host, const std::string &details) override
	{
		(void)host;
		(void)details;
		return false;
	}
};


// A repository with one commit on main ("a.txt", "b.txt") and a second
// branch "feature" one commit ahead that changes a.txt.
class Fixture {
private:
	std::string fBase;

public:
	std::string directory;
	RepositoryPtr repository;
	git_oid first {};
	git_oid second {};

	Fixture()
	{
		fBase = MakeTemporaryDirectory();
		directory = fBase + "/main";
		git_repository_init_options options = GIT_REPOSITORY_INIT_OPTIONS_INIT;
		options.flags = GIT_REPOSITORY_INIT_MKPATH;
		options.initial_head = "main";
		git_repository *created = nullptr;
		Check(git_repository_init_ext(&created, directory.c_str(), &options), "init");
		repository.reset(created);
		git_config *config = nullptr;
		Check(git_repository_config(&config, created), "config");
		ConfigPtr configPtr(config);
		Check(git_config_set_string(config, "user.name", "Test"), "name");
		Check(git_config_set_string(config, "user.email", "test@example.com"), "email");

		WriteFile(directory + "/a.txt", "a1\n");
		WriteFile(directory + "/b.txt", "b1\n");
		first = CommitAll(created, "first");
		Run(PlanCreateBranch(created, Load(), "feature", "main", ""), created);
		// Advance feature without touching the main worktree.
		Run(PlanCreateWorktree(created, Load(), Path("feature"), {WorktreeStartKind::ExistingBranch, "feature", "", ""}),
			created);
		RepositoryPtr feature = OpenWorktree(created, *Load().FindWorktree(Path("feature")));
		WriteFile(Path("feature") + "/a.txt", "a2\n");
		second = CommitAll(feature.get(), "second");
		feature.reset();
		Run(PlanRemoveWorktree(created, *Load().FindWorktree(Path("feature")), false), created);
	}

	~Fixture()
	{
		repository.reset();
		std::filesystem::remove_all(fBase);
	}

	// A path next to the main worktree.
	std::string Path(const std::string &name) const {return fBase + "/" + name;}
	git_repository *Get() const {return repository.get();}
	Snapshot Load() const {return Snapshot::Load(repository.get());}
};


TEST_CASE("Snapshot lists worktrees, branches and tags", "[gittool]")
{
	Fixture fixture;
	Run(PlanCreateTag(fixture.Get(), fixture.Load(), "v1", "main", ""), fixture.Get());
	Run(PlanCreateTag(fixture.Get(), fixture.Load(), "v2", "feature", "Release 2"), fixture.Get());
	Run(PlanCreateWorktree(fixture.Get(), fixture.Load(), fixture.Path("wt"),
		{WorktreeStartKind::ExistingBranch, "feature", "", ""}), fixture.Get());

	const Snapshot snapshot = fixture.Load();
	REQUIRE(snapshot.worktrees.size() == 2);
	CHECK(snapshot.worktrees[0].IsMain());
	CHECK(snapshot.worktrees[0].path == fixture.directory);
	CHECK(snapshot.worktrees[0].branch == "main");
	CHECK(snapshot.worktrees[1].path == fixture.Path("wt"));
	CHECK(snapshot.worktrees[1].branch == "feature");
	CHECK_FALSE(snapshot.worktrees[1].HasChanges());

	REQUIRE(snapshot.branches.size() == 2);
	CHECK(snapshot.branches[0].name == "feature");
	CHECK(snapshot.branches[0].worktree == fixture.Path("wt"));
	CHECK(snapshot.branches[1].name == "main");
	CHECK(snapshot.branches[1].summary == "first");

	REQUIRE(snapshot.tags.size() == 2);
	CHECK_FALSE(snapshot.tags[0].annotated);
	CHECK(snapshot.tags[1].annotated);
	CHECK(snapshot.tags[1].message == "Release 2");
	CHECK(git_oid_equal(&snapshot.tags[1].commit, &fixture.second));

	// Opening a linked worktree opens the whole repository.
	RepositoryPtr opened = OpenRepository(fixture.Path("wt"));
	CHECK(Snapshot::Load(opened.get()).worktrees.size() == 2);
}


TEST_CASE("Set commit of a branch without a worktree", "[gittool]")
{
	Fixture fixture;
	Plan plan = PlanSetBranchCommit(fixture.Get(), fixture.Load(), "feature", "main");
	CHECK(plan.worktrees.empty());
	REQUIRE(plan.unreachableCount == 1);
	CHECK(plan.unreachable[0].summary == "second");
	Run(plan, fixture.Get());
	const git_oid moved = RefId(fixture.Get(), "refs/heads/feature");
	CHECK(git_oid_equal(&moved, &fixture.first));

	// Undo through previous positions.
	const std::vector<ReflogEntry> positions = PreviousPositions(fixture.Get(), "refs/heads/feature");
	REQUIRE(!positions.empty());
	CHECK(git_oid_equal(&positions[0].oldId, &fixture.second));
	Run(PlanSetBranchCommit(fixture.Get(), fixture.Load(), "feature", OidString(positions[0].oldId)), fixture.Get());
	const git_oid restored = RefId(fixture.Get(), "refs/heads/feature");
	CHECK(git_oid_equal(&restored, &fixture.second));
}


TEST_CASE("Set commit of the branch of a worktree updates its files", "[gittool]")
{
	Fixture fixture;
	const std::string a = fixture.directory + "/a.txt";
	const std::string b = fixture.directory + "/b.txt";

	SECTION("Clean") {
		Plan plan = PlanSetBranchCommit(fixture.Get(), fixture.Load(), "main", "feature");
		REQUIRE(plan.worktrees.size() == 1);
		CHECK(plan.worktrees[0].check.state == WorktreeState::Clean);
		Run(plan, fixture.Get());
		CHECK(ReadFile(a) == "a2\n");
		CHECK_FALSE(fixture.Load().worktrees[0].HasChanges());
	}

	SECTION("Changes that do not overlap are carried over") {
		WriteFile(b, "b-changed\n");
		WriteFile(fixture.directory + "/c.txt", "staged\n");
		Stage(fixture.Get(), "c.txt");
		Plan plan = PlanSetBranchCommit(fixture.Get(), fixture.Load(), "main", "feature");
		REQUIRE(plan.worktrees.size() == 1);
		CHECK(plan.worktrees[0].check.state == WorktreeState::Changes);
		Run(plan, fixture.Get());
		CHECK(ReadFile(a) == "a2\n");
		CHECK(ReadFile(b) == "b-changed\n");
		const WorktreeInfo worktree = fixture.Load().worktrees[0];
		CHECK(worktree.staged == 1);
		CHECK(worktree.changed == 1);
	}

	SECTION("Overlapping changes need a resolution") {
		WriteFile(a, "a-local\n");
		Plan plan = PlanSetBranchCommit(fixture.Get(), fixture.Load(), "main", "feature");
		REQUIRE(plan.NeedsResolution());
		CHECK(plan.worktrees[0].check.files == std::vector<std::string> {"a.txt"});
		RunContext context;
		context.repository = fixture.Get();
		CHECK_THROWS_AS(plan.run(context), GitError);
		CHECK(git_oid_equal(&fixture.first, &fixture.Load().FindBranch("main")->commit));

		SECTION("Move position only") {
			Run(plan, fixture.Get(), Resolution::PositionOnly);
			CHECK(ReadFile(a) == "a-local\n");
			CHECK(git_oid_equal(&fixture.second, &fixture.Load().FindBranch("main")->commit));
		}

		SECTION("Discard") {
			Run(plan, fixture.Get(), Resolution::Discard);
			CHECK(ReadFile(a) == "a2\n");
			CHECK_FALSE(fixture.Load().worktrees[0].HasChanges());
		}
	}

	SECTION("Untracked files that would be overwritten overlap") {
		Run(PlanSetBranchCommit(fixture.Get(), fixture.Load(), "feature", "main"), fixture.Get());
		RepositoryPtr featureRepository;
		{
			Run(PlanCreateWorktree(fixture.Get(), fixture.Load(), fixture.Path("wt"),
				{WorktreeStartKind::ExistingBranch, "feature", "", ""}), fixture.Get());
			featureRepository = OpenWorktree(fixture.Get(), *fixture.Load().FindWorktree(fixture.Path("wt")));
		}
		WriteFile(fixture.Path("wt") + "/new.txt", "new\n");
		const git_oid third = CommitAll(featureRepository.get(), "third");
		(void)third;
		WriteFile(fixture.directory + "/new.txt", "untracked\n");
		Plan plan = PlanSetBranchCommit(fixture.Get(), fixture.Load(), "main", "feature");
		REQUIRE(plan.NeedsResolution());
	}
}


TEST_CASE("Worktree positions", "[gittool]")
{
	Fixture fixture;

	SECTION("New worktree on a commit has no branch") {
		Run(PlanCreateWorktree(fixture.Get(), fixture.Load(), fixture.Path("detached"),
			{WorktreeStartKind::Commit, "", OidString(fixture.second), ""}), fixture.Get());
		const Snapshot snapshot = fixture.Load();
		const WorktreeInfo *worktree = snapshot.FindWorktree(fixture.Path("detached"));
		REQUIRE(worktree != nullptr);
		CHECK(worktree->branch.empty());
		CHECK(git_oid_equal(&*worktree->commit, &fixture.second));
		CHECK(snapshot.branches.size() == 2);
		CHECK(ReadFile(fixture.Path("detached") + "/a.txt") == "a2\n");

		// Moving away loses nothing: feature has the commit.
		Plan plan = PlanSetWorktreeCommit(fixture.Get(), snapshot, *worktree, "main");
		CHECK(plan.unreachableCount == 0);
		Run(plan, fixture.Get());
		CHECK(ReadFile(fixture.Path("detached") + "/a.txt") == "a1\n");
	}

	SECTION("New worktree on a new branch") {
		Run(PlanCreateWorktree(fixture.Get(), fixture.Load(), fixture.Path("topic"),
			{WorktreeStartKind::NewBranch, "topic", "feature", ""}), fixture.Get());
		const Snapshot snapshot = fixture.Load();
		REQUIRE(snapshot.FindBranch("topic") != nullptr);
		CHECK(snapshot.FindBranch("topic")->worktree == fixture.Path("topic"));
	}

	SECTION("Set branch that another worktree has") {
		Run(PlanCreateWorktree(fixture.Get(), fixture.Load(), fixture.Path("wt"),
			{WorktreeStartKind::ExistingBranch, "feature", "", ""}), fixture.Get());
		Snapshot snapshot = fixture.Load();
		Plan plan = PlanSetWorktreeBranch(fixture.Get(), snapshot, snapshot.worktrees[0], "feature");
		Run(plan, fixture.Get());
		snapshot = fixture.Load();
		CHECK(snapshot.worktrees[0].branch == "feature");
		CHECK(ReadFile(fixture.directory + "/a.txt") == "a2\n");
		const WorktreeInfo *other = snapshot.FindWorktree(fixture.Path("wt"));
		CHECK(other->branch.empty());
		CHECK(git_oid_equal(&*other->commit, &fixture.second));
	}

	SECTION("Create branch here") {
		Snapshot snapshot = fixture.Load();
		Run(PlanSetWorktreeCommit(fixture.Get(), snapshot, snapshot.worktrees[0], "feature"), fixture.Get());
		snapshot = fixture.Load();
		CHECK(snapshot.worktrees[0].branch.empty());
		Run(PlanCreateBranchHere(fixture.Get(), snapshot, snapshot.worktrees[0], "here"), fixture.Get());
		CHECK(fixture.Load().worktrees[0].branch == "here");
	}
}


TEST_CASE("Worktree changes", "[gittool]")
{
	Fixture fixture;
	const std::string a = fixture.directory + "/a.txt";
	WriteFile(a, "changed\n");
	Stage(fixture.Get(), "a.txt");
	WriteFile(fixture.directory + "/untracked.txt", "x\n");

	SECTION("Unstage all") {
		Run(PlanUnstageAll(fixture.Get(), fixture.Load().worktrees[0]), fixture.Get());
		const WorktreeInfo worktree = fixture.Load().worktrees[0];
		CHECK(worktree.staged == 0);
		CHECK(worktree.changed == 1);
		CHECK(ReadFile(a) == "changed\n");
	}

	SECTION("Discard changes") {
		Plan plan = PlanDiscardChanges(fixture.Get(), fixture.Load().worktrees[0], true);
		CHECK(plan.destructive);
		Run(plan, fixture.Get());
		CHECK(ReadFile(a) == "a1\n");
		CHECK_FALSE(Exists(fixture.directory + "/untracked.txt"));
		CHECK_FALSE(fixture.Load().worktrees[0].HasChanges());
	}
}


TEST_CASE("Remove, protect and forget worktrees", "[gittool]")
{
	Fixture fixture;
	const std::string path = fixture.Path("wt");
	Run(PlanCreateWorktree(fixture.Get(), fixture.Load(), path, {WorktreeStartKind::ExistingBranch, "feature", "", ""}),
		fixture.Get());

	SECTION("Remove refuses changes unless forced") {
		WriteFile(path + "/a.txt", "changed\n");
		const WorktreeInfo worktree = *fixture.Load().FindWorktree(path);
		CHECK_FALSE(PlanRemoveWorktree(fixture.Get(), worktree, false).blocked.empty());
		Run(PlanRemoveWorktree(fixture.Get(), worktree, true), fixture.Get());
		CHECK_FALSE(Exists(path));
		CHECK(fixture.Load().worktrees.size() == 1);
		CHECK(fixture.Load().FindBranch("feature") != nullptr);
	}

	SECTION("Protect") {
		Run(PlanProtectWorktree(*fixture.Load().FindWorktree(path), true, "on a removable drive"), fixture.Get());
		const WorktreeInfo worktree = *fixture.Load().FindWorktree(path);
		CHECK(worktree.locked);
		CHECK(worktree.lockReason == "on a removable drive");
		CHECK_FALSE(PlanRemoveWorktree(fixture.Get(), worktree, true).blocked.empty());
	}

	SECTION("Forget missing") {
		std::filesystem::remove_all(path);
		Snapshot snapshot = fixture.Load();
		const WorktreeInfo *worktree = snapshot.FindWorktree(path);
		REQUIRE(worktree != nullptr);
		CHECK(worktree->missing);
		CHECK(worktree->branch == "feature");
		// Its branch is still taken, and its files cannot be updated.
		Plan plan = PlanSetBranchCommit(fixture.Get(), snapshot, "feature", "main");
		CHECK(plan.FilesLocked());
		Run(PlanForgetMissingWorktrees(fixture.Get(), snapshot), fixture.Get());
		CHECK(fixture.Load().worktrees.size() == 1);
	}
}


TEST_CASE("Branch operations", "[gittool]")
{
	Fixture fixture;

	SECTION("Rename follows the worktree") {
		Run(PlanRenameBranch(fixture.Get(), fixture.Load(), "main", "trunk"), fixture.Get());
		const Snapshot snapshot = fixture.Load();
		CHECK(snapshot.worktrees[0].branch == "trunk");
		CHECK(snapshot.FindBranch("main") == nullptr);
	}

	SECTION("Delete reports lost commits") {
		Plan plan = PlanDeleteBranches(fixture.Get(), fixture.Load(), {"feature"});
		CHECK(plan.destructive);
		CHECK(plan.unreachableCount == 1);
		Run(plan, fixture.Get());
		CHECK(fixture.Load().FindBranch("feature") == nullptr);
	}

	SECTION("Delete the branch of a worktree sets it to the commit") {
		Run(PlanDeleteBranches(fixture.Get(), fixture.Load(), {"main"}), fixture.Get());
		const Snapshot snapshot = fixture.Load();
		CHECK(snapshot.worktrees[0].branch.empty());
		CHECK(git_oid_equal(&*snapshot.worktrees[0].commit, &fixture.first));
	}

	SECTION("Fast-forward, merge and replay") {
		CHECK(PlanMergeInto(fixture.Get(), fixture.Load(), "feature", "main").blocked.size() > 0);

		// main gets its own commit, so feature and main diverge.
		WriteFile(fixture.directory + "/b.txt", "b2\n");
		const git_oid mainCommit = CommitAll(fixture.Get(), "main change");

		SECTION("Replay") {
			Plan plan = PlanReplayOnto(fixture.Get(), fixture.Load(), "feature", "main");
			Run(plan, fixture.Get());
			const BranchInfo feature = *fixture.Load().FindBranch("feature");
			CHECK(git_graph_descendant_of(fixture.Get(), &feature.commit, &mainCommit) == 1);
			const CommitInfo info = ReadCommit(fixture.Get(), feature.commit);
			CHECK(info.summary == "second");
		}

		SECTION("Merge into a branch without a worktree") {
			Run(PlanMergeInto(fixture.Get(), fixture.Load(), "feature", "main"), fixture.Get());
			const BranchInfo feature = *fixture.Load().FindBranch("feature");
			git_commit *commit = nullptr;
			Check(git_commit_lookup(&commit, fixture.Get(), &feature.commit), "lookup");
			CommitPtr commitPtr(commit);
			CHECK(git_commit_parentcount(commit) == 2);
		}

		SECTION("Merge into the branch of a worktree updates its files") {
			Run(PlanMergeInto(fixture.Get(), fixture.Load(), "main", "feature"), fixture.Get());
			CHECK(ReadFile(fixture.directory + "/a.txt") == "a2\n");
			CHECK(ReadFile(fixture.directory + "/b.txt") == "b2\n");
			CHECK_FALSE(fixture.Load().worktrees[0].HasChanges());
		}

		SECTION("Conflicts are refused") {
			WriteFile(fixture.directory + "/a.txt", "a-main\n");
			CommitAll(fixture.Get(), "conflicting");
			CHECK_THROWS_AS(PlanMergeInto(fixture.Get(), fixture.Load(), "main", "feature"), GitError);
			CHECK_THROWS_AS(PlanReplayOnto(fixture.Get(), fixture.Load(), "feature", "main"), GitError);
		}
	}

	SECTION("Clean up candidates") {
		Run(PlanCreateBranch(fixture.Get(), fixture.Load(), "old", "main", ""), fixture.Get());
		const auto candidates = CleanupCandidates(fixture.Get(), fixture.Load(), "feature");
		REQUIRE(candidates.size() == 1);
		CHECK(candidates[0].first == "old");
	}
}


TEST_CASE("Tags", "[gittool]")
{
	Fixture fixture;
	Run(PlanCreateTag(fixture.Get(), fixture.Load(), "v1", "main", "Version 1"), fixture.Get());
	Run(PlanMoveTag(fixture.Get(), fixture.Load(), "v1", "feature"), fixture.Get());
	CHECK(git_oid_equal(&fixture.Load().FindTag("v1")->commit, &fixture.second));
	CHECK(fixture.Load().FindTag("v1")->message == "Version 1");

	Run(PlanRenameTag(fixture.Load(), "v1", "v1.0"), fixture.Get());
	const Snapshot snapshot = fixture.Load();
	CHECK(snapshot.FindTag("v1") == nullptr);
	REQUIRE(snapshot.FindTag("v1.0") != nullptr);
	CHECK(snapshot.FindTag("v1.0")->annotated);

	Run(PlanEditTagMessage(fixture.Load(), "v1.0", ""), fixture.Get());
	CHECK_FALSE(fixture.Load().FindTag("v1.0")->annotated);
	Run(PlanDeleteTag(fixture.Load(), "v1.0"), fixture.Get());
	CHECK(fixture.Load().tags.empty());
}


TEST_CASE("Remotes", "[gittool]")
{
	Fixture fixture;
	TestHandler handler;
	const std::string serverPath = fixture.Path("server.git");
	git_repository *server = nullptr;
	Check(git_repository_init(&server, serverPath.c_str(), true), "init server");
	RepositoryPtr serverPtr(server);

	Run(PlanAddRemote(fixture.Get(), fixture.Load(), "origin", serverPath, ""), fixture.Get());
	Run(PlanUploadBranch(fixture.Get(), fixture.Load(), "main", "origin", UploadMode::Normal, true), fixture.Get(),
		Resolution::Cancel, &handler);
	CHECK(HasRef(server, "refs/heads/main"));
	{
		const BranchInfo main = *fixture.Load().FindBranch("main");
		CHECK(main.upstream == "origin/main");
		CHECK(main.ahead == 0);
	}

	SECTION("Overwrite only if unchanged") {
		// The server moves on without a download.
		Run(PlanUploadBranch(fixture.Get(), fixture.Load(), "feature", "origin", UploadMode::Normal, false),
			fixture.Get(), Resolution::Cancel, &handler);
		MoveRef(server, "refs/heads/main", fixture.second);
		WriteFile(fixture.directory + "/b.txt", "b3\n");
		CommitAll(fixture.Get(), "local");
		Plan plan = PlanUploadBranch(fixture.Get(), fixture.Load(), "main", "origin", UploadMode::OverwriteIfUnchanged,
			false);
		RunContext context;
		context.repository = fixture.Get();
		context.network = &handler;
		CHECK_THROWS_AS(plan.run(context), GitError);

		// After a download the remote copy has the server state.
		Run(PlanDownload(fixture.Load(), "origin", false, false), fixture.Get(), Resolution::Cancel, &handler);
		const BranchInfo main = *fixture.Load().FindBranch("main");
		CHECK(main.behind == 1);
		CHECK(main.ahead == 1);
		Run(PlanUploadBranch(fixture.Get(), fixture.Load(), "main", "origin", UploadMode::OverwriteIfUnchanged, false),
			fixture.Get(), Resolution::Cancel, &handler);
		const git_oid local = RefId(fixture.Get(), "refs/heads/main");
		const git_oid uploaded = RefId(server, "refs/heads/main");
		CHECK(git_oid_equal(&local, &uploaded));
	}

	SECTION("Update from tracked") {
		Run(PlanUploadBranch(fixture.Get(), fixture.Load(), "feature", "origin", UploadMode::Normal, false),
			fixture.Get(), Resolution::Cancel, &handler);
		MoveRef(server, "refs/heads/main", fixture.second);
		Run(PlanDownload(fixture.Load(), "", false, false), fixture.Get(), Resolution::Cancel, &handler);
		Run(PlanUpdateAllFromTracked(fixture.Get(), fixture.Load()), fixture.Get());
		CHECK(ReadFile(fixture.directory + "/a.txt") == "a2\n");
		CHECK(fixture.Load().FindBranch("main")->behind == 0);
	}

	SECTION("Server check and delete on the server") {
		Run(PlanCreateTag(fixture.Get(), fixture.Load(), "v1", "main", ""), fixture.Get());
		Run(PlanUploadBranch(fixture.Get(), fixture.Load(), "feature", "origin", UploadMode::Normal, false),
			fixture.Get(), Resolution::Cancel, &handler);
		CHECK(fixture.Load().FindRemoteCopy("origin/feature") != nullptr);
		const std::vector<std::string> before = CompareWithServer(fixture.Get(), fixture.Load(), "origin",
			ListServer(fixture.Get(), "origin", handler));
		CHECK(before == std::vector<std::string> {"Tag v1: only local"});

		Run(PlanUploadTags(fixture.Load(), "origin", {"v1"}), fixture.Get(), Resolution::Cancel, &handler);
		Run(PlanDeleteOnServer(fixture.Load(), "origin", "refs/heads/feature"), fixture.Get(), Resolution::Cancel,
			&handler);
		CHECK_FALSE(HasRef(server, "refs/heads/feature"));
		CHECK(HasRef(server, "refs/tags/v1"));
		CHECK(fixture.Load().FindRemoteCopy("origin/feature") == nullptr);
	}

	SECTION("Rename and remove") {
		Run(PlanRenameRemote(fixture.Load(), "origin", "upstream"), fixture.Get());
		CHECK(fixture.Load().FindBranch("main")->upstream == "upstream/main");
		Run(PlanRemoveRemote(fixture.Load(), "upstream"), fixture.Get());
		const Snapshot snapshot = fixture.Load();
		CHECK(snapshot.remotes.empty());
		CHECK(snapshot.FindBranch("main")->upstream.empty());
	}
}


TEST_CASE("Commit changes", "[gittool]")
{
	Fixture fixture;
	git_repository *repository = fixture.Get();

	const std::vector<FileChange> root = CommitChanges(repository, fixture.first, 0);
	REQUIRE(root.size() == 2);
	CHECK(root[0].status == 'A');
	CHECK(root[0].Path() == "a.txt");
	CHECK(root[0].oldPath.empty());

	const std::vector<FileChange> modified = CommitChanges(repository, fixture.second, 0);
	REQUIRE(modified.size() == 1);
	CHECK(modified[0].status == 'M');
	CHECK(ReadFileVersion(repository, modified[0].oldId, modified[0].oldMode) == "a1\n");
	CHECK(ReadFileVersion(repository, modified[0].newId, modified[0].newMode) == "a2\n");

	std::filesystem::rename(fixture.directory + "/b.txt", fixture.directory + "/renamed.txt");
	WriteFile(fixture.directory + "/new.txt", "new\n");
	const git_oid third = CommitAll(repository, "rename\n\nWith a body.");
	const std::vector<FileChange> renamed = CommitChanges(repository, third, 0);
	REQUIRE(renamed.size() == 2);
	CHECK(renamed[0].status == 'A');
	CHECK(renamed[0].newPath == "new.txt");
	CHECK_FALSE(ReadFileVersion(repository, renamed[0].oldId, renamed[0].oldMode).has_value());
	CHECK(renamed[1].status == 'R');
	CHECK(renamed[1].oldPath == "b.txt");
	CHECK(renamed[1].newPath == "renamed.txt");

	const CommitDetails details = ReadCommitDetails(repository, third);
	CHECK(details.message == "rename\n\nWith a body.");
	CHECK(details.author.name == "Test");
	REQUIRE(details.parents.size() == 1);
	CHECK(git_oid_equal(&details.parents[0], &fixture.first));
}


} // GitTool
