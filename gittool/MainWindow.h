#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <Messenger.h>
#include <Node.h>
#include <Window.h>

#include "NetworkTask.h"
#include "Plan.h"
#include "Snapshot.h"


class BGroupView;
class BColumnListView;
class BButton;
class BListView;
class BMenu;
class BMessageRunner;
class BStringView;
class BTextView;


namespace GitTool {


enum class Category {
	Worktrees,
	Branches,
	Remotes,
	Tags,
};


enum class ObjectKind {
	None,
	Worktree,
	Branch,
	Remote,
	RemoteCopy,
	Tag,
};


enum class Action;


// One repository: its worktrees, branches, remotes and tags, with the
// operations on them.
class MainWindow: public BWindow {
private:
	RepositoryPtr fRepository;
	// The repository database (common directory), reopened by network
	// tasks.
	std::string fRepositoryPath;
	Snapshot fSnapshot;
	Category fCategory = Category::Worktrees;

	BListView *fCategories {};
	BColumnListView *fObjects {};
	BTextView *fDetails {};
	BTextView *fLog {};
	BStringView *fStatus {};
	BButton *fCancelButton {};
	BGroupView *fButtons {};
	BMenu *fActionsMenu {};

	std::unique_ptr<NetworkTask> fTask;
	std::unique_ptr<BMessageRunner> fRefreshRunner;
	std::vector<node_ref> fWatched;
	std::map<std::string, BMessenger> fDiffWindows;

	void ShowCategoryButtons();
	void Watch();
	void Refresh();
	void ScheduleRefresh();
	void FillCategories();
	void FillObjects();
	void ShowDetails();
	void UpdateTitle();

	ObjectKind SelectedKind() const;
	std::string SelectedName() const;
	const WorktreeInfo *SelectedWorktree() const;
	const BranchInfo *SelectedBranch() const;
	const RemoteInfo *SelectedRemote() const;
	const RemoteCopyInfo *SelectedRemoteCopy() const;
	const TagInfo *SelectedTag() const;
	// The selected tag of that name with its details read.
	TagInfo *LoadedTag(const std::string &name);
	// Text of a column of the tag list, read on first draw.
	std::string TagText(size_t index, int32 column);

	void AddActions(BMenu *menu);
	void ShowContextMenu();
	void RunAction(Action action);
	void RunWorktreeAction(Action action);
	void RunBranchAction(Action action);
	void RunRemoteAction(Action action);
	void RunTagAction(Action action);
	void ObjectInvoked();

	// Previews and runs a plan; network plans run in the background.
	void Execute(const Plan &plan);
	void StartNetwork(const std::string &status, NetworkTask::Work work, const BMessage &done);
	void NetworkDone(BMessage *message);
	void AppendLog(const std::string &text);

	// Choices for forms.
	std::vector<std::string> BranchNames() const;
	std::vector<std::string> RemoteNames() const;
	bool ChooseWorktree(const std::string &title, const std::string &okLabel, const std::string &exceptPath,
		WorktreeInfo &worktree);
	bool ChooseRemote(const std::string &title, const std::string &okLabel, std::string &remote);
	std::string DefaultCommit() const;

	void CreateWorktree(const std::string &branch);
	void UpdateFromTracked(const BranchInfo &branch);
	void CleanUpBranches();
	void ShowPreviousPositions(const BranchInfo &branch);
	void UploadTags();
	void OpenDiff(const WorktreeInfo &worktree);
	void OpenFolder(const std::string &path);

public:
	explicit MainWindow(RepositoryPtr repository);
	~MainWindow() override;

	const std::string &RepositoryPath() const {return fRepositoryPath;}

	void MessageReceived(BMessage *message) override;
	void MenusBeginning() override;
	void WindowActivated(bool active) override;
	bool QuitRequested() override;
};


} // GitTool
