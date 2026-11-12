#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Messenger.h>
#include <Node.h>
#include <Window.h>

#include "BackgroundLoader.h"
#include "Dialogs.h"
#include "Operations.h"
#include "NetworkReporter.h"
#include "Plan.h"
#include "Snapshot.h"


class BGroupView;
class BColumnListView;
class BButton;
class BListView;
class BMenu;
class BMessageRunner;
class BStringView;
class BTextControl;
class BTextView;


namespace GitTool {


class DetailsView;


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
	// Whether a snapshot was read since the window opened.
	bool fLoaded {};
	Category fCategory = Category::Worktrees;
	// Snapshots are read on a thread; each request has a number, and work
	// waiting for a refresh runs once a snapshot of its request arrives.
	std::unique_ptr<DiffKit::BackgroundLoader<std::pair<uint64, Snapshot>>> fSnapshotLoader;
	uint64 fRefreshRequest {};
	// The request of the snapshot shown: older ones are not shown.
	uint64 fShownRequest {};
	std::vector<std::pair<uint64, std::function<void()>>> fAfterRefresh;
	BTextControl *fTagFilter {};
	// Closing during a network operation asks without blocking.
	bool fAskingClose {};
	bool fCloseConfirmed {};
	std::string fTagFilterText;

	BListView *fCategories {};
	BColumnListView *fObjects {};
	DetailsView *fDetails {};
	BTextView *fLog {};
	BStringView *fStatus {};
	BGroupView *fButtons {};
	BMenu *fActionsMenu {};

	// A network operation runs (under its progress dialog).
	bool fNetworkRunning {};
	// Gets the outcome of the running network operation instead of an
	// alert, for a form that stays open until it works.
	std::function<void(const std::string &error)> fNetworkFinished;
	std::unique_ptr<BMessageRunner> fRefreshRunner;
	std::vector<node_ref> fWatched;
	std::map<std::string, BMessenger> fDiffWindows;

	void ShowCategoryButtons();
	void Watch();
	// Reads the repository again on a thread; the window is updated when the
	// snapshot arrives.
	void Refresh();
	// The first snapshot is read under a progress dialog.
	void LoadFirst();
	void SnapshotLoaded();
	void ShowSnapshot(uint64 request, Snapshot snapshot);
	// Runs work once the snapshot of the latest refresh is shown.
	void AfterRefresh(std::function<void()> work);
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

	// Previews a plan in a dialog and runs it if the user agrees, under a
	// progress dialog; network plans then run done (kMsgNetworkDone with its
	// follow-up) through NetworkDone(). then runs once the snapshot after a
	// local plan is shown.
	void Execute(const Plan &plan, std::function<void()> then = nullptr);
	void Execute(const Plan &plan, const BMessage &done);
	void RunPlan(const Plan &plan, Resolution resolution, const BMessage &done, std::function<void()> then);
	// Runs work, showing what it throws (dialog answers run outside
	// RunAction()).
	void Guarded(const std::function<void()> &work);
	// Opens a form over this window; accepted runs with its values. With
	// commitPicker, its commit fields get the picker.
	void AskForm(const Form &form, std::function<void(const Form &form)> accepted, bool commitPicker = false);
	void PickCommitThen(const std::string &spec, std::function<void(const std::string &spec)> chosen);
	void ChooseRowsThen(const std::string &title, const std::string &okLabel, const std::vector<ListColumn> &columns,
		const std::vector<std::vector<std::string>> &rows, bool multiple,
		std::function<void(const std::vector<int> &selected)> chosen);
	void UploadForReview(const BranchInfo &branch);
	void DownloadChange(const RemoteInfo &remote);
	void DownloadChange(const RemoteInfo &remote, const Form &form,
		std::function<void(const std::string &error)> finished);
	// Report lines are shown to the user when the work finishes.
	using NetworkWork = std::function<void(git_repository *repository, NetworkHandler &handler,
		std::vector<std::string> &report)>;
	// Runs work on a thread under a progress dialog; then NetworkDone() gets
	// done with "error" added.
	void StartNetwork(const std::string &status, NetworkWork work, const BMessage &done);
	void NetworkDone(BMessage *message, const std::vector<std::string> &report);
	void NetworkFollowUp(const BMessage *message, const std::vector<std::string> &report, const std::string &title);
	void AppendLog(const std::string &text);

	// Choices for forms.
	std::vector<std::string> BranchNames() const;
	std::vector<std::string> RemoteNames() const;
	// These ask in a dialog unless there is no choice; chosen runs with the
	// choice.
	void ChooseWorktree(const std::string &title, const std::string &okLabel, const std::string &exceptPath,
		std::function<void(const WorktreeInfo &worktree)> chosen);
	void ChooseRemote(const std::string &title, const std::string &okLabel,
		std::function<void(const std::string &remote)> chosen);
	std::string DefaultCommit() const;

	void CreateWorktree(const std::string &branch);
	void UpdateFromTracked(const BranchInfo &branch);
	void UpdateFromTracked(const BranchInfo &branch, UpdateMode mode);
	void CleanUpBranches();
	void ShowPreviousPositions(const BranchInfo &branch);
	void UploadTags();
	void OpenDiff(const WorktreeInfo &worktree);
	void OpenFolder(const std::string &path);
	// A history window; edit starts in edit mode for a local branch.
	void ShowHistory(const std::string &spec, bool edit, const std::string &onto = std::string());
	// A commit window over this one.
	void ShowCommit(const git_oid &commit);
	// Replays a branch onto a commit: with the usual preview, or in the
	// history editor if it has conflicts.
	void RebaseBranch(const std::string &branch, const std::string &onto);
	void AskToResolveRebase(const std::string &branch, const std::string &onto);

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
