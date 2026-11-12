#include "MainWindow.h"

#include <ctype.h>

#include <exception>
#include <functional>

#include <Alert.h>
#include <Application.h>
#include <Button.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <Entry.h>
#include <GroupView.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <MessageRunner.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "DiffView.h"
#include "DiffWindow.h"
#include "Ask.h"
#include "Dialogs.h"
#include "HistoryEdit.h"
#include "HistoryWindow.h"
#include "Messages.h"
#include "Operations.h"
#include "Progress.h"
#include "Repository.h"
#include "WindowHelpers.h"


namespace GitTool {


using DiffKit::ShowError;


// Delay before refreshing after a change on disk; changes come in bursts.
static constexpr bigtime_t kRefreshDelay = 300000;
// Rows of tags shown at most: Haiku's column list view adds and removes rows
// in quadratic time, and a list of tens of thousands of tags is no use
// without the filter anyway.
static constexpr size_t kTagLimit = 1000;

static constexpr uint32 kMsgSnapshotLoaded = 'GTSL';
static constexpr uint32 kMsgLoadFirst = 'GTLF';
static constexpr uint32 kMsgTagFilter = 'GTtf';


enum class Action {
	CreateWorktree,
	ForgetMissingWorktrees,
	WorktreeOpenDiff,
	WorktreeOpenFolder,
	WorktreeSetBranch,
	WorktreeSetCommit,
	WorktreeCreateBranch,
	WorktreeUnstageAll,
	WorktreeDiscard,
	WorktreeRemove,
	WorktreeProtect,
	WorktreeUnprotect,
	WorktreeAbort,

	CreateBranch,
	UpdateAllFromTracked,
	CleanUpBranches,
	BranchSetCommit,
	BranchUpdateFromTracked,
	BranchMerge,
	BranchReplay,
	BranchRebaseInteractive,
	BranchUpload,
	BranchUploadForReview,
	BranchSetUpstream,
	BranchStopTracking,
	BranchAddTag,
	BranchSetInWorktree,
	BranchNewWorktree,
	BranchRename,
	BranchDelete,
	BranchPreviousPositions,
	BranchUndo,
	BranchHistory,
	BranchEditHistory,

	AddRemote,
	DownloadAll,
	RemoteDownload,
	RemoteDownloadChange,
	RemoteEdit,
	RemoteRename,
	RemoteRemove,
	RemoteCheckServer,
	CopyCreateBranch,
	CopySetTracked,
	CopySetWorktree,
	CopyDeleteOnServer,
	CopyHistory,

	CreateTag,
	UploadTags,
	DownloadTags,
	TagMove,
	TagEditMessage,
	TagRename,
	TagUpload,
	TagDelete,
	TagDeleteOnServer,
	TagCreateBranch,
	TagSetWorktree,
	TagHistory,
};


struct ActionItem {
	Action action;
	// nullptr for a separator.
	const char *label;
};


#define ELLIPSIS B_UTF8_ELLIPSIS

static const ActionItem kSeparator = {Action::CreateWorktree, nullptr};

static const std::vector<ActionItem> kCategoryActions[] = {
	{
		{Action::CreateWorktree, "Create worktree" ELLIPSIS},
		{Action::ForgetMissingWorktrees, "Forget missing worktrees" ELLIPSIS},
	},
	{
		{Action::CreateBranch, "Create branch" ELLIPSIS},
		{Action::UpdateAllFromTracked, "Update all from tracked" ELLIPSIS},
		{Action::CleanUpBranches, "Clean up" ELLIPSIS},
	},
	{
		{Action::AddRemote, "Add remote" ELLIPSIS},
		{Action::DownloadAll, "Download from all"},
	},
	{
		{Action::CreateTag, "Create tag" ELLIPSIS},
		{Action::UploadTags, "Upload tags" ELLIPSIS},
		{Action::DownloadTags, "Download tags" ELLIPSIS},
	},
};

static const std::vector<ActionItem> kWorktreeActions = {
	{Action::WorktreeOpenDiff, "Open in Git Diff"},
	{Action::WorktreeOpenFolder, "Open folder"},
	kSeparator,
	{Action::WorktreeSetBranch, "Set branch" ELLIPSIS},
	{Action::WorktreeSetCommit, "Set commit (no branch)" ELLIPSIS},
	{Action::WorktreeCreateBranch, "Create branch here" ELLIPSIS},
	kSeparator,
	{Action::WorktreeUnstageAll, "Unstage all" ELLIPSIS},
	{Action::WorktreeDiscard, "Discard changes" ELLIPSIS},
	{Action::WorktreeAbort, "Abort unfinished operation" ELLIPSIS},
	kSeparator,
	{Action::WorktreeProtect, "Protect" ELLIPSIS},
	{Action::WorktreeUnprotect, "Unprotect" ELLIPSIS},
	{Action::WorktreeRemove, "Remove worktree" ELLIPSIS},
};

static const std::vector<ActionItem> kBranchActions = {
	{Action::BranchSetCommit, "Set commit" ELLIPSIS},
	{Action::BranchUpdateFromTracked, "Update from tracked" ELLIPSIS},
	{Action::BranchMerge, "Merge another branch into this" ELLIPSIS},
	{Action::BranchReplay, "Replay onto" ELLIPSIS},
	{Action::BranchRebaseInteractive, "Replay interactively onto" ELLIPSIS},
	kSeparator,
	{Action::BranchUpload, "Upload" ELLIPSIS},
	{Action::BranchUploadForReview, "Upload for review (Gerrit)" ELLIPSIS},
	{Action::BranchSetUpstream, "Set tracked remote branch" ELLIPSIS},
	{Action::BranchStopTracking, "Stop tracking" ELLIPSIS},
	kSeparator,
	{Action::BranchSetInWorktree, "Set in worktree" ELLIPSIS},
	{Action::BranchNewWorktree, "Set in new worktree" ELLIPSIS},
	{Action::BranchAddTag, "Add tag at this branch's commit" ELLIPSIS},
	{Action::BranchRename, "Rename" ELLIPSIS},
	{Action::BranchDelete, "Delete" ELLIPSIS},
	kSeparator,
	{Action::BranchPreviousPositions, "Previous positions" ELLIPSIS},
	{Action::BranchUndo, "Undo last change" ELLIPSIS},
	{Action::BranchHistory, "Show history"},
	{Action::BranchEditHistory, "Edit history" ELLIPSIS},
};

static const std::vector<ActionItem> kRemoteActions = {
	{Action::RemoteDownload, "Download" ELLIPSIS},
	{Action::RemoteDownloadChange, "Download review change (Gerrit)" ELLIPSIS},
	{Action::RemoteCheckServer, "Check what's on the server"},
	kSeparator,
	{Action::RemoteEdit, "Edit URLs" ELLIPSIS},
	{Action::RemoteRename, "Rename" ELLIPSIS},
	{Action::RemoteRemove, "Remove" ELLIPSIS},
};

static const std::vector<ActionItem> kRemoteCopyActions = {
	{Action::CopyCreateBranch, "Create local branch from this" ELLIPSIS},
	{Action::CopySetTracked, "Set as tracked for" ELLIPSIS},
	{Action::CopySetWorktree, "Set worktree to this commit" ELLIPSIS},
	{Action::CopyDeleteOnServer, "Delete on server" ELLIPSIS},
	kSeparator,
	{Action::CopyHistory, "Show history"},
};

static const std::vector<ActionItem> kTagActions = {
	{Action::TagMove, "Move to commit" ELLIPSIS},
	{Action::TagEditMessage, "Edit message" ELLIPSIS},
	{Action::TagRename, "Rename" ELLIPSIS},
	kSeparator,
	{Action::TagUpload, "Upload" ELLIPSIS},
	{Action::TagDelete, "Delete locally" ELLIPSIS},
	{Action::TagDeleteOnServer, "Delete on server" ELLIPSIS},
	kSeparator,
	{Action::TagCreateBranch, "Create branch from tag" ELLIPSIS},
	{Action::TagSetWorktree, "Set worktree to this commit" ELLIPSIS},
	{Action::TagHistory, "Show history"},
};


enum class FollowUp {
	None,
	UpdateFromTracked,
	Report,
	// Shows the history of a downloaded change: "remote", "change" and
	// "branch" (empty for none).
	ShowChange,
};


static BMessage *ActionMessage(Action action)
{
	BMessage *message = new BMessage(kMsgAction);
	message->AddInt32("action", static_cast<int32>(action));
	return message;
}


static void AddItems(BMenu *menu, const std::vector<ActionItem> &items)
{
	for (const ActionItem &item : items) {
		if (item.label == nullptr) {
			menu->AddSeparatorItem();
		} else {
			menu->AddItem(new BMenuItem(item.label, ActionMessage(item.action)));
		}
	}
}


//#pragma mark - Object list

class ObjectRow: public BRow {
public:
	ObjectKind kind;
	std::string name;

	ObjectRow(ObjectKind rowKind, const std::string &rowName):
		kind(rowKind),
		name(rowName)
	{
	}
};


// A text read when the field is first drawn.
class LazyField: public BStringField {
private:
	std::function<std::string()> fRead;

public:
	explicit LazyField(std::function<std::string()> read):
		BStringField(""),
		fRead(std::move(read))
	{
	}

	void Fill()
	{
		if (fRead) {
			SetString(fRead().c_str());
			fRead = nullptr;
		}
	}
};


// Opens the context menu on a secondary click; fills lazy fields.
class ObjectColumn: public BStringColumn {
public:
	ObjectColumn(const char *title, float width):
		BStringColumn(title, width, 30, 3000, B_TRUNCATE_END)
	{
		SetWantsEvents(true);
	}

	void MouseDown(BColumnListView *parent, BRow *row, BField *field, BRect fieldRect, BPoint point,
		uint32 buttons) final
	{
		(void)field;
		(void)fieldRect;
		(void)point;
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) == 0 || row == nullptr) {
			return;
		}
		parent->DeselectAll();
		parent->AddToSelection(row);
		parent->Window()->PostMessage(kMsgContextMenu);
	}

	void DrawField(BField *field, BRect rect, BView *parent) final
	{
		if (LazyField *lazy = dynamic_cast<LazyField*>(field)) {
			lazy->Fill();
		}
		BStringColumn::DrawField(field, rect, parent);
	}
};


static bool SameTags(const std::vector<TagInfo> &a, const std::vector<TagInfo> &b)
{
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); i++) {
		if (a[i].name != b[i].name || !git_oid_equal(&a[i].target, &b[i].target)) {
			return false;
		}
	}
	return true;
}


static constexpr int32 kColumnCount = 5;


static const char *kColumnTitles[][kColumnCount] = {
	{"Folder", "Position", "State", nullptr, nullptr},
	{"Branch", "Commit", "Tracks", "Worktree", "Updated"},
	{"Remote", "Commit or URL", nullptr, nullptr, nullptr},
	{"Tag", "Commit", "Kind", "Message", nullptr},
};


static std::string StateText(const WorktreeInfo &worktree)
{
	if (worktree.missing) {
		return worktree.locked ? "folder missing, protected" : "folder missing";
	}
	std::string text;
	auto add = [&text](const std::string &part) {
		text += (text.empty() ? "" : ", ") + part;
	};
	if (!worktree.operation.empty()) {
		add(worktree.operation + " in progress");
	}
	if (worktree.conflicts > 0) {
		add(std::to_string(worktree.conflicts) + " conflicts");
	}
	if (worktree.staged > 0) {
		add(std::to_string(worktree.staged) + " staged");
	}
	if (worktree.changed > 0) {
		add(std::to_string(worktree.changed) + " changed");
	}
	if (worktree.untracked > 0) {
		add(std::to_string(worktree.untracked) + " untracked");
	}
	if (text.empty()) {
		text = "clean";
	}
	if (worktree.locked) {
		add("protected");
	}
	return text;
}


static std::string PositionText(const WorktreeInfo &worktree)
{
	if (!worktree.branch.empty()) {
		return "on branch " + worktree.branch;
	}
	if (worktree.commit.has_value()) {
		return "on commit " + ShortId(*worktree.commit) + " (no branch)";
	}
	return "no commit";
}


static std::string TrackingText(const BranchInfo &branch)
{
	if (branch.upstream.empty()) {
		return std::string();
	}
	if (branch.upstreamGone) {
		return branch.upstream + " (gone)";
	}
	std::string text = branch.upstream;
	if (branch.ahead > 0) {
		text += " ↑" + std::to_string(branch.ahead);
	}
	if (branch.behind > 0) {
		text += " ↓" + std::to_string(branch.behind);
	}
	return text;
}


static std::string FirstLine(const std::string &text)
{
	return text.substr(0, text.find('\n'));
}


//#pragma mark - MainWindow

MainWindow::MainWindow(RepositoryPtr repository):
	BWindow(
		BRect(40, 40, 979, 679),
		"Git",
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRepository(std::move(repository)),
	fRepositoryPath(git_repository_commondir(fRepository.get()))
{
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("Repository")
			.AddItem("Open repository" ELLIPSIS, kMsgOpenRepository, 'O')
			.AddItem("Refresh", kMsgRefresh, 'R')
			.AddItem("Download from all", ActionMessage(Action::DownloadAll), 'D')
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
			.AddItem("Quit", kMsgQuitApplication, 'Q')
		.End()
		.AddMenu("Actions")
			.GetMenu(fActionsMenu)
		.End();

	fCategories = new BListView("categories", B_SINGLE_SELECTION_LIST);
	fCategories->SetSelectionMessage(new BMessage(kMsgCategorySelected));
	for (const char *name : {"Worktrees", "Branches", "Remotes", "Tags"}) {
		fCategories->AddItem(new BStringItem(name));
	}

	fObjects = new BColumnListView("objects", B_NAVIGABLE, B_PLAIN_BORDER, true);
	fObjects->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	const float widths[] = {260, 220, 180, 160, 130};
	for (int32 i = 0; i < kColumnCount; i++) {
		fObjects->AddColumn(new ObjectColumn("", widths[i]), i);
	}
	fObjects->SetSortingEnabled(false);
	fObjects->SetSelectionMessage(new BMessage(kMsgObjectSelected));
	fObjects->SetInvocationMessage(new BMessage(kMsgObjectInvoked));

	fSnapshotLoader = std::make_unique<DiffKit::BackgroundLoader<std::pair<uint64, Snapshot>>>(BMessenger(this),
		kMsgSnapshotLoaded);
	fButtons = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
	ShowCategoryButtons();

	fDetails = new BTextView("details");
	fDetails->MakeEditable(false);
	fDetails->SetWordWrap(true);
	fLog = new BTextView("log");
	fLog->MakeEditable(false);
	fLog->SetWordWrap(true);
	BScrollView *detailScroll = new BScrollView("detailScroll", fDetails, 0, false, true);
	BScrollView *logScroll = new BScrollView("logScroll", fLog, 0, false, true);
	BScrollView *categoryScroll = new BScrollView("categoryScroll", fCategories, 0, false, false);
	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	// A split view ignores preferred sizes: it divides its space by the
	// weights (limited by the minimum sizes), so the weights are the initial
	// sizes in pixels for the window's initial size.
	BSplitView *rightSplit = new BSplitView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Split<>(rightSplit)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 300)
			.Add(fButtons)
			.Add(fObjects)
		.End()
		.Add(detailScroll, 150)
		.Add(logScroll, 100);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.AddSplit(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(categoryScroll, 130)
				.Add(rightSplit, 790)
			.End()
			.Add(fStatus)
		.End();

	Watch();
	// Once the window is shown.
	PostMessage(kMsgLoadFirst);
	fCategories->Select(0);
	UpdateTitle();
	MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
}


MainWindow::~MainWindow()
{
	stop_watching(this);
}


// The buttons of the category operations.
void MainWindow::ShowCategoryButtons()
{
	while (BView *child = fButtons->ChildAt(0)) {
		child->RemoveSelf();
		delete child;
	}
	BGroupLayout *layout = fButtons->GroupLayout();
	while (layout->CountItems() > 0) {
		delete layout->RemoveItem(static_cast<int32>(0));
	}
	for (const ActionItem &item : kCategoryActions[static_cast<int>(fCategory)]) {
		fButtons->AddChild(new BButton(item.label, ActionMessage(item.action)));
	}
	layout->AddItem(BSpaceLayoutItem::CreateGlue());
	fTagFilter = nullptr;
	if (fCategory == Category::Tags) {
		fTagFilter = new BTextControl("tagFilter", "Filter:", fTagFilterText.c_str(), nullptr);
		fTagFilter->SetModificationMessage(new BMessage(kMsgTagFilter));
		fTagFilter->SetExplicitMinSize(BSize(160, B_SIZE_UNSET));
		fButtons->AddChild(fTagFilter);
	}
}


void MainWindow::Watch()
{
	stop_watching(this);
	fWatched.clear();
	for (const char *subdirectory : {"", "refs/heads", "refs/tags", "worktrees"}) {
		BEntry entry((fRepositoryPath + subdirectory).c_str());
		node_ref node;
		if (entry.GetNodeRef(&node) == B_OK) {
			watch_node(&node, B_WATCH_DIRECTORY, this);
			fWatched.push_back(node);
		}
	}
}


void MainWindow::UpdateTitle()
{
	std::string path = fRepositoryPath;
	if (const char *workdir = git_repository_workdir(fRepository.get())) {
		path = workdir;
	}
	while (path.size() > 1 && path.back() == '/') {
		path.pop_back();
	}
	SetTitle((BPath(path.c_str()).Leaf() + std::string(" — Git")).c_str());
}


void MainWindow::Refresh()
{
	fRefreshRunner.reset();
	const uint64 request = ++fRefreshRequest;
	const std::string path = fRepositoryPath;
	fSnapshotLoader->Load([path, request]() {
		RepositoryPtr repository = OpenRepository(path);
		return std::pair<uint64, Snapshot>(request, Snapshot::Load(repository.get()));
	});
}


void MainWindow::AfterRefresh(std::function<void()> work)
{
	fAfterRefresh.emplace_back(fRefreshRequest, std::move(work));
}


void MainWindow::SnapshotLoaded()
{
	std::string error;
	std::optional<std::pair<uint64, Snapshot>> loaded = fSnapshotLoader->Take(error);
	if (!loaded.has_value()) {
		if (!error.empty()) {
			AppendLog("Cannot read the repository: " + error);
		}
		return;
	}
	ShowSnapshot(loaded->first, std::move(loaded->second));
}


void MainWindow::LoadFirst()
{
	const uint64 request = ++fRefreshRequest;
	const std::string path = fRepositoryPath;
	std::shared_ptr<Snapshot> loaded = std::make_shared<Snapshot>();
	DiffKit::RunWithProgress(this, "Reading the repository", [path, loaded](DiffKit::Progress &progress) {
		(void)progress;
		RepositoryPtr repository = OpenRepository(path);
		*loaded = Snapshot::Load(repository.get());
	}, [this, request, loaded](const std::string &error) {
		if (!error.empty()) {
			AppendLog("Cannot read the repository: " + error);
			ShowError(error.c_str());
			return;
		}
		ShowSnapshot(request, std::move(*loaded));
	});
}


void MainWindow::ShowSnapshot(uint64 request, Snapshot snapshot)
{
	if (request < fShownRequest) {
		return;
	}
	fShownRequest = request;
	// Unchanged tags keep their details, and their rows are not rebuilt.
	const bool sameTags = fLoaded && SameTags(fSnapshot.tags, snapshot.tags);
	if (sameTags) {
		snapshot.tags = std::move(fSnapshot.tags);
	}
	fSnapshot = std::move(snapshot);
	fLoaded = true;
	FillCategories();
	if (fCategory != Category::Tags || !sameTags) {
		FillObjects();
	}
	ShowDetails();

	std::vector<std::function<void()>> ready;
	for (auto it = fAfterRefresh.begin(); it != fAfterRefresh.end();) {
		if (it->first <= request) {
			ready.push_back(std::move(it->second));
			it = fAfterRefresh.erase(it);
		} else {
			++it;
		}
	}
	for (std::function<void()> &work : ready) {
		try {
			work();
		} catch (const std::exception &e) {
			ShowError(e.what());
		}
	}
}


void MainWindow::ScheduleRefresh()
{
	BMessage message(kMsgRefreshTimer);
	fRefreshRunner = std::make_unique<BMessageRunner>(BMessenger(this), &message, kRefreshDelay, 1);
}


void MainWindow::FillCategories()
{
	const size_t counts[] = {
		fSnapshot.worktrees.size(),
		fSnapshot.branches.size(),
		fSnapshot.remotes.size(),
		fSnapshot.tags.size(),
	};
	static const char *kNames[] = {"Worktrees", "Branches", "Remotes", "Tags"};
	for (int32 i = 0; i < 4; i++) {
		BStringItem *item = static_cast<BStringItem*>(fCategories->ItemAt(i));
		const std::string count = fLoaded ? std::to_string(counts[i]) : std::string(B_UTF8_ELLIPSIS);
		item->SetText((std::string(kNames[i]) + " (" + count + ")").c_str());
	}
	fCategories->Invalidate();
}


void MainWindow::FillObjects()
{
	const ObjectKind selectedKind = SelectedKind();
	const std::string selectedName = SelectedName();
	fObjects->Clear();

	const int category = static_cast<int>(fCategory);
	for (int32 i = 0; i < kColumnCount; i++) {
		const char *title = kColumnTitles[category][i];
		BTitledColumn *column = static_cast<BTitledColumn*>(fObjects->ColumnAt(i));
		column->SetTitle(title != nullptr ? title : "");
		fObjects->SetColumnVisible(i, title != nullptr);
	}
	// Changed titles are not redrawn by the title view itself.
	for (int32 i = 0; BView *child = fObjects->ChildAt(i); i++) {
		child->Invalidate();
	}

	// Top rows are added together at the end: one by one, each addition
	// takes time in proportion to the rows before it.
	BList topRows;
	auto addRow = [this, &topRows](ObjectKind kind, const std::string &name, const std::vector<std::string> &fields,
		BRow *parent = nullptr) {
		ObjectRow *row = new ObjectRow(kind, name);
		for (size_t i = 0; i < fields.size(); i++) {
			row->SetField(new BStringField(fields[i].c_str()), static_cast<int32>(i));
		}
		if (parent != nullptr) {
			fObjects->AddRow(row, parent);
		} else {
			topRows.AddItem(row);
		}
		return row;
	};

	switch (fCategory) {
		case Category::Worktrees:
			for (const WorktreeInfo &worktree : fSnapshot.worktrees) {
				addRow(ObjectKind::Worktree, worktree.path,
					{worktree.path + (worktree.IsMain() ? " (main)" : ""), PositionText(worktree), StateText(worktree)});
			}
			break;
		case Category::Branches:
			for (const BranchInfo &branch : fSnapshot.branches) {
				addRow(ObjectKind::Branch, branch.name, {branch.name, ShortId(branch.commit) + " " + branch.summary,
					TrackingText(branch), branch.worktree, FormatTime(branch.updated)});
			}
			break;
		case Category::Remotes:
			for (const RemoteInfo &remote : fSnapshot.remotes) {
				// The children need their parent in the list.
				ObjectRow *parent = new ObjectRow(ObjectKind::Remote, remote.name);
				parent->SetField(new BStringField(remote.name.c_str()), 0);
				parent->SetField(new BStringField(remote.url.c_str()), 1);
				fObjects->AddRow(parent);
				for (const RemoteCopyInfo &copy : remote.copies) {
					addRow(ObjectKind::RemoteCopy, copy.name, {copy.name, ShortId(copy.commit) + " " + copy.summary},
						parent);
				}
				fObjects->ExpandOrCollapse(parent, true);
			}
			break;
		case Category::Tags: {
			// Newest (highest) names first; at most kTagLimit of those that
			// contain the filter text, ignoring case.
			std::string filter;
			for (char c : fTagFilterText) {
				filter += static_cast<char>(tolower(static_cast<unsigned char>(c)));
			}
			size_t matches = 0;
			for (size_t i = fSnapshot.tags.size(); i-- > 0;) {
				const std::string &name = fSnapshot.tags[i].name;
				if (!filter.empty()) {
					std::string lower;
					for (char c : name) {
						lower += static_cast<char>(tolower(static_cast<unsigned char>(c)));
					}
					if (lower.find(filter) == std::string::npos) {
						continue;
					}
				}
				if (++matches > kTagLimit) {
					continue;
				}
				ObjectRow *row = new ObjectRow(ObjectKind::Tag, name);
				row->SetField(new BStringField(name.c_str()), 0);
				for (int32 column = 1; column <= 3; column++) {
					row->SetField(new LazyField([this, i, column]() {
						return TagText(i, column);
					}), column);
				}
				topRows.AddItem(row);
			}
			if (matches > kTagLimit) {
				ObjectRow *row = new ObjectRow(ObjectKind::None, std::string());
				row->SetField(new BStringField((B_UTF8_ELLIPSIS " " + std::to_string(matches - kTagLimit)
					+ " more; type in the filter to find them").c_str()), 0);
				topRows.AddItem(row);
			}
			break;
		}
	}
	fObjects->AddRows(&topRows, -1, nullptr);

	for (int32 i = 0; i < fObjects->CountRows(); i++) {
		ObjectRow *row = static_cast<ObjectRow*>(fObjects->RowAt(i));
		for (int32 j = -1; j < fObjects->CountRows(row); j++) {
			ObjectRow *candidate = j < 0 ? row : static_cast<ObjectRow*>(fObjects->RowAt(j, row));
			if (candidate->kind == selectedKind && candidate->name == selectedName) {
				fObjects->AddToSelection(candidate);
				fObjects->ScrollTo(candidate);
			}
		}
	}
	if (fObjects->CurrentSelection() == nullptr && fObjects->CountRows() > 0) {
		fObjects->AddToSelection(fObjects->RowAt(0));
	}
}


void MainWindow::ShowDetails()
{
	std::string text;
	auto line = [&text](const std::string &label, const std::string &value) {
		if (!value.empty()) {
			text += label + ": " + value + "\n";
		}
	};
	git_repository *repository = fRepository.get();
	if (const WorktreeInfo *worktree = SelectedWorktree()) {
		line("Folder", worktree->path);
		line("Kind", worktree->IsMain() ? "main worktree" : "linked worktree \"" + worktree->name + "\"");
		line("Position", PositionText(*worktree));
		if (worktree->commit.has_value()) {
			line("Commit", Describe(repository, *worktree->commit));
		}
		line("State", StateText(*worktree));
		if (worktree->locked) {
			line("Protected", worktree->lockReason.empty() ? "yes" : worktree->lockReason);
		}
	} else if (const BranchInfo *branch = SelectedBranch()) {
		line("Branch", branch->name);
		line("Commit", Describe(repository, branch->commit));
		if (!branch->upstream.empty()) {
			line("Tracks", branch->upstream + (branch->upstreamGone ? " (no longer exists)" : ""));
			if (!branch->upstreamGone) {
				line("Compared to it", std::to_string(branch->ahead) + " ahead, " + std::to_string(branch->behind)
					+ " behind");
			}
		}
		line("Worktree", branch->worktree.empty() ? "none" : branch->worktree);
		line("Last updated", FormatTime(branch->updated));
	} else if (const RemoteInfo *remote = SelectedRemote()) {
		line("Remote", remote->name);
		line("Download URL", remote->url);
		line("Upload URL", remote->pushUrl.empty() ? remote->url : remote->pushUrl);
		line("Last download", remote->lastDownload != 0 ? FormatTime(remote->lastDownload) : "unknown");
		line("Remote copies", std::to_string(remote->copies.size()));
	} else if (const RemoteCopyInfo *copy = SelectedRemoteCopy()) {
		line("Remote copy", copy->name + " (last downloaded state)");
		line("Commit", Describe(repository, copy->commit));
		std::string tracking;
		for (const BranchInfo &candidate : fSnapshot.branches) {
			if (candidate.upstream == copy->name) {
				tracking += (tracking.empty() ? "" : ", ") + candidate.name;
			}
		}
		line("Tracked by", tracking);
	} else if (const TagInfo *tag = LoadedTag(SelectedName())) {
		line("Tag", tag->name);
		line("Commit", Describe(repository, tag->commit));
		line("Kind", tag->annotated ? "with message" : "simple");
		line("Tagger", tag->tagger);
		line("Date", FormatTime(tag->time));
		if (!tag->message.empty()) {
			text += "\n" + tag->message;
		}
	}
	fDetails->SetText(text.c_str());
}


ObjectKind MainWindow::SelectedKind() const
{
	const ObjectRow *row = dynamic_cast<ObjectRow*>(fObjects->CurrentSelection());
	return row != nullptr ? row->kind : ObjectKind::None;
}


std::string MainWindow::SelectedName() const
{
	const ObjectRow *row = dynamic_cast<ObjectRow*>(fObjects->CurrentSelection());
	return row != nullptr ? row->name : std::string();
}


const WorktreeInfo *MainWindow::SelectedWorktree() const
{
	return SelectedKind() == ObjectKind::Worktree ? fSnapshot.FindWorktree(SelectedName()) : nullptr;
}


const BranchInfo *MainWindow::SelectedBranch() const
{
	return SelectedKind() == ObjectKind::Branch ? fSnapshot.FindBranch(SelectedName()) : nullptr;
}


const RemoteInfo *MainWindow::SelectedRemote() const
{
	return SelectedKind() == ObjectKind::Remote ? fSnapshot.FindRemote(SelectedName()) : nullptr;
}


const RemoteCopyInfo *MainWindow::SelectedRemoteCopy() const
{
	return SelectedKind() == ObjectKind::RemoteCopy ? fSnapshot.FindRemoteCopy(SelectedName()) : nullptr;
}


TagInfo *MainWindow::LoadedTag(const std::string &name)
{
	if (SelectedKind() != ObjectKind::Tag) {
		return nullptr;
	}
	for (TagInfo &tag : fSnapshot.tags) {
		if (tag.name == name) {
			try {
				LoadTagDetails(fRepository.get(), tag);
			} catch (const std::exception &e) {
				AppendLog(e.what());
			}
			return &tag;
		}
	}
	return nullptr;
}


std::string MainWindow::TagText(size_t index, int32 column)
{
	if (index >= fSnapshot.tags.size()) {
		return std::string();
	}
	TagInfo &tag = fSnapshot.tags[index];
	try {
		LoadTagDetails(fRepository.get(), tag);
	} catch (const std::exception &e) {
		return e.what();
	}
	switch (column) {
		case 1:
			return tag.hasCommit ? ShortId(tag.commit) + " " + tag.summary : tag.summary;
		case 2:
			return tag.annotated ? "with message" : "simple";
		default:
			return FirstLine(tag.message);
	}
}


const TagInfo *MainWindow::SelectedTag() const
{
	return SelectedKind() == ObjectKind::Tag ? fSnapshot.FindTag(SelectedName()) : nullptr;
}


void MainWindow::AddActions(BMenu *menu)
{
	switch (SelectedKind()) {
		case ObjectKind::Worktree:
			AddItems(menu, kWorktreeActions);
			break;
		case ObjectKind::Branch:
			AddItems(menu, kBranchActions);
			break;
		case ObjectKind::Remote:
			AddItems(menu, kRemoteActions);
			break;
		case ObjectKind::RemoteCopy:
			AddItems(menu, kRemoteCopyActions);
			break;
		case ObjectKind::Tag:
			AddItems(menu, kTagActions);
			break;
		case ObjectKind::None:
			break;
	}
	if (menu->CountItems() > 0) {
		menu->AddSeparatorItem();
	}
	AddItems(menu, kCategoryActions[static_cast<int>(fCategory)]);
}


void MainWindow::MenusBeginning()
{
	BWindow::MenusBeginning();
	fActionsMenu->RemoveItems(0, fActionsMenu->CountItems(), true);
	AddActions(fActionsMenu);
}


void MainWindow::ShowContextMenu()
{
	BPopUpMenu *menu = new BPopUpMenu("context", false, false);
	AddActions(menu);
	menu->SetTargetForItems(this);
	BPoint where;
	uint32 buttons;
	get_mouse(&where, &buttons);
	menu->SetAsyncAutoDestruct(true);
	menu->Go(where, true, true, true);
}


void MainWindow::ObjectInvoked()
{
	switch (SelectedKind()) {
		case ObjectKind::Worktree:
			RunAction(Action::WorktreeOpenDiff);
			break;
		case ObjectKind::Branch:
		case ObjectKind::RemoteCopy:
		case ObjectKind::Tag:
			RunAction(SelectedKind() == ObjectKind::Branch ? Action::BranchHistory
				: SelectedKind() == ObjectKind::Tag ? Action::TagHistory : Action::CopyHistory);
			break;
		default:
			break;
	}
}


//#pragma mark - Running operations

void MainWindow::Execute(const Plan &plan, std::function<void()> then)
{
	ConfirmPlan(this, plan, fRepository.get(), [this, plan, then = std::move(then)](Resolution resolution) {
		RunPlan(plan, resolution, BMessage(kMsgNetworkDone), then);
	});
}


void MainWindow::Execute(const Plan &plan, const BMessage &done)
{
	ConfirmPlan(this, plan, fRepository.get(), [this, plan, done](Resolution resolution) {
		RunPlan(plan, resolution, done, nullptr);
	});
}


void MainWindow::Guarded(const std::function<void()> &work)
{
	try {
		work();
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
}


void MainWindow::AskForm(const Form &form, std::function<void(const Form &form)> accepted, bool commitPicker)
{
	form.Open(this, [this, accepted = std::move(accepted)](const Form &result) {
		Guarded([&]() {
			accepted(result);
		});
	}, commitPicker ? fRepository.get() : nullptr, commitPicker ? &fSnapshot : nullptr);
}


void MainWindow::PickCommitThen(const std::string &spec, std::function<void(const std::string &spec)> chosen)
{
	PickCommit(this, fRepository.get(), fSnapshot, spec, [this, chosen = std::move(chosen)](const std::string &result) {
		Guarded([&]() {
			chosen(result);
		});
	});
}


void MainWindow::ChooseRowsThen(const std::string &title, const std::string &okLabel,
	const std::vector<ListColumn> &columns, const std::vector<std::vector<std::string>> &rows, bool multiple,
	std::function<void(const std::vector<int> &selected)> chosen)
{
	ChooseFromList(this, title, okLabel, columns, rows, multiple,
		[this, chosen = std::move(chosen)](const std::vector<int> &selected) {
			Guarded([&]() {
				chosen(selected);
			});
		});
}


void MainWindow::RunPlan(const Plan &plan, Resolution resolution, const BMessage &done, std::function<void()> then)
{
	if (plan.network) {
		StartNetwork(plan.title, [plan, resolution](git_repository *repository, NetworkHandler &handler,
			std::vector<std::string> &report) {
			(void)report;
			RunContext context;
			context.repository = repository;
			context.resolution = resolution;
			context.network = &handler;
			plan.run(context);
		}, done);
		return;
	}
	RunPlanWithProgress(this, fRepositoryPath, plan, resolution,
		[this, title = plan.title, then = std::move(then)](const std::string &error) {
			if (error.empty()) {
				AppendLog("Done: " + title);
			} else {
				AppendLog("Failed: " + title + ": " + error);
				ShowError(error.c_str());
			}
			Refresh();
			if (then) {
				AfterRefresh([this, then]() {
					Guarded(then);
				});
			}
		});
}


void MainWindow::StartNetwork(const std::string &status, NetworkWork work, const BMessage &done)
{
	// Dialogs do not block the window, so their answers can come meanwhile.
	if (fNetworkRunning) {
		ShowError("Another network operation is running; try again when it ends");
		return;
	}
	BMessage message(done);
	if (!message.HasString("title")) {
		message.AddString("title", status.c_str());
	}
	fStatus->SetText((status + B_UTF8_ELLIPSIS).c_str());
	AppendLog(status);
	fNetworkRunning = true;
	const std::string path = fRepositoryPath;
	const BMessenger logTarget(this);
	std::shared_ptr<std::vector<std::string>> report = std::make_shared<std::vector<std::string>>();
	DiffKit::RunWithProgress(this, status, [path, logTarget, work = std::move(work), report](DiffKit::Progress &progress) {
		NetworkReporter reporter(progress, logTarget);
		RepositoryPtr repository = OpenRepository(path);
		work(repository.get(), reporter, *report);
	}, [this, message, report](const std::string &error) {
		fNetworkRunning = false;
		BMessage finished(message);
		finished.AddString("error", error.c_str());
		NetworkDone(&finished, *report);
	}, true);
}


void MainWindow::NetworkDone(BMessage *message, const std::vector<std::string> &report)
{
	const std::string title = message->GetString("title", "");
	const std::string error = message->GetString("error", "");
	const std::function<void(const std::string &error)> finished = std::move(fNetworkFinished);
	fNetworkFinished = nullptr;
	if (!error.empty()) {
		fStatus->SetText(("Failed: " + title).c_str());
		AppendLog("Failed: " + title + ": " + error);
		Refresh();
		if (finished) {
			finished(error);
		} else {
			ShowError(error.c_str());
		}
		return;
	}
	fStatus->SetText(("Done: " + title).c_str());
	AppendLog("Done: " + title);
	if (finished) {
		finished(std::string());
	}
	Refresh();
	// The follow-ups use the snapshot after the operation.
	AfterRefresh([this, followUp = BMessage(*message), report, title]() {
		Guarded([&]() {
			NetworkFollowUp(&followUp, report, title);
		});
	});
}


void MainWindow::NetworkFollowUp(const BMessage *message, const std::vector<std::string> &report,
	const std::string &title)
{
	switch (static_cast<FollowUp>(message->GetInt32("followUp", 0))) {
		case FollowUp::UpdateFromTracked: {
			const std::string branch = message->GetString("branch", "");
			const UpdateMode mode = static_cast<UpdateMode>(message->GetInt32("mode", 0));
			const BranchInfo *info = fSnapshot.FindBranch(branch);
			if (mode == UpdateMode::Replay && info != nullptr && !info->upstreamGone && !info->upstream.empty()) {
				RebaseBranch(branch, info->upstream);
			} else {
				Execute(PlanUpdateFromTracked(fRepository.get(), fSnapshot, branch, mode));
			}
			break;
		}
		case FollowUp::Report: {
			std::string text;
			for (const std::string &line : report) {
				text += line + "\n";
			}
			ShowText(this, title, text.empty() ? "The local copies match the server." : text);
			break;
		}
		case FollowUp::ShowChange: {
			const std::string branch = message->GetString("branch", "");
			if (!branch.empty()) {
				ShowHistory(branch, false);
				break;
			}
			const std::string remote = message->GetString("remote", "");
			const int32 change = message->GetInt32("change", 0);
			const int32 patchset = message->GetInt32("patchset", 0);
			const std::optional<std::string> ref = patchset > 0
				? std::optional<std::string>(LocalChangeRef(remote, change, patchset))
				: LatestLocalChange(fRepository.get(), remote, change);
			if (ref.has_value()) {
				ShowHistory(*ref, false);
			}
			break;
		}
		case FollowUp::None:
			break;
	}
}


void MainWindow::AppendLog(const std::string &text)
{
	const std::string line = (fLog->TextLength() > 0 ? "\n" : "") + text;
	fLog->Insert(fLog->TextLength(), line.c_str(), line.size());
	fLog->ScrollToOffset(fLog->TextLength());
}


//#pragma mark - Choices

std::vector<std::string> MainWindow::BranchNames() const
{
	std::vector<std::string> names;
	for (const BranchInfo &branch : fSnapshot.branches) {
		names.push_back(branch.name);
	}
	return names;
}


std::vector<std::string> MainWindow::RemoteNames() const
{
	std::vector<std::string> names;
	for (const RemoteInfo &remote : fSnapshot.remotes) {
		names.push_back(remote.name);
	}
	return names;
}


void MainWindow::ChooseWorktree(const std::string &title, const std::string &okLabel, const std::string &exceptPath,
	std::function<void(const WorktreeInfo &worktree)> chosen)
{
	std::vector<WorktreeInfo> candidates;
	std::vector<std::string> labels;
	for (const WorktreeInfo &candidate : fSnapshot.worktrees) {
		if (candidate.path != exceptPath && !candidate.missing) {
			candidates.push_back(candidate);
			labels.push_back(candidate.path + " (" + PositionText(candidate) + ")");
		}
	}
	if (candidates.empty()) {
		ShowError("There is no other worktree");
		return;
	}
	Form form(title, okLabel);
	form.AddChoice("worktree", "Worktree:", labels);
	AskForm(form, [candidates, chosen = std::move(chosen)](const Form &result) {
		chosen(candidates[result.Choice("worktree")]);
	});
}


void MainWindow::ChooseRemote(const std::string &title, const std::string &okLabel,
	std::function<void(const std::string &remote)> chosen)
{
	const std::vector<std::string> names = RemoteNames();
	if (names.empty()) {
		ShowError("There are no remotes");
		return;
	}
	if (names.size() == 1 && okLabel.empty()) {
		chosen(names[0]);
		return;
	}
	Form form(title, okLabel.empty() ? "OK" : okLabel);
	form.AddChoice("remote", "Remote:", names);
	AskForm(form, [names, chosen = std::move(chosen)](const Form &result) {
		chosen(names[result.Choice("remote")]);
	});
}


// The commit of the main worktree, as a starting point for new branches and
// tags.
std::string MainWindow::DefaultCommit() const
{
	if (const BranchInfo *branch = SelectedBranch()) {
		return branch->name;
	}
	for (const WorktreeInfo &worktree : fSnapshot.worktrees) {
		if (!worktree.branch.empty()) {
			return worktree.branch;
		}
		if (worktree.commit.has_value()) {
			return OidString(*worktree.commit);
		}
	}
	return std::string();
}


//#pragma mark - Actions

void MainWindow::RunAction(Action action)
{
	try {
		switch (action) {
			case Action::CreateWorktree:
			case Action::ForgetMissingWorktrees:
			case Action::WorktreeOpenDiff:
			case Action::WorktreeOpenFolder:
			case Action::WorktreeSetBranch:
			case Action::WorktreeSetCommit:
			case Action::WorktreeCreateBranch:
			case Action::WorktreeUnstageAll:
			case Action::WorktreeDiscard:
			case Action::WorktreeRemove:
			case Action::WorktreeProtect:
			case Action::WorktreeUnprotect:
			case Action::WorktreeAbort:
				RunWorktreeAction(action);
				break;
			case Action::AddRemote:
			case Action::DownloadAll:
			case Action::RemoteDownload:
			case Action::RemoteDownloadChange:
			case Action::RemoteEdit:
			case Action::RemoteRename:
			case Action::RemoteRemove:
			case Action::RemoteCheckServer:
			case Action::CopyCreateBranch:
			case Action::CopySetTracked:
			case Action::CopySetWorktree:
			case Action::CopyDeleteOnServer:
			case Action::CopyHistory:
				RunRemoteAction(action);
				break;
			case Action::CreateTag:
			case Action::UploadTags:
			case Action::DownloadTags:
			case Action::TagMove:
			case Action::TagEditMessage:
			case Action::TagRename:
			case Action::TagUpload:
			case Action::TagDelete:
			case Action::TagDeleteOnServer:
			case Action::TagCreateBranch:
			case Action::TagSetWorktree:
			case Action::TagHistory:
				RunTagAction(action);
				break;
			default:
				RunBranchAction(action);
				break;
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
}


void MainWindow::CreateWorktree(const std::string &branch)
{
	static const std::vector<std::string> kStarts = {
		"On an existing branch",
		"On a new branch",
		"On a commit, with no branch",
	};
	std::string folder;
	if (!fSnapshot.worktrees.empty()) {
		const std::string &main = fSnapshot.worktrees[0].path;
		folder = main.substr(0, main.rfind('/') + 1);
	}
	Form form("Create worktree", "OK");
	form.AddText("path", "Folder:", folder + branch)
		.AddChoice("start", "Start:", kStarts, 0)
		.AddText("branch", "Branch:", branch)
		.AddCommit("commit", "Start commit:", branch.empty() ? DefaultCommit() : branch)
		.AddText("upstream", "New branch tracks:", "")
		.AddLabel("A new branch starts at the start commit; \"tracks\" is an optional remote copy, as origin/main.");
	AskForm(form, [this](const Form &result) {
		WorktreeStart start;
		start.kind = static_cast<WorktreeStartKind>(result.Choice("start"));
		start.branch = result.Text("branch");
		start.commit = result.Text("commit");
		start.upstream = result.Text("upstream");
		Execute(PlanCreateWorktree(fRepository.get(), fSnapshot, result.Text("path"), start));
	}, true);
}


void MainWindow::RunWorktreeAction(Action action)
{
	git_repository *repository = fRepository.get();
	if (action == Action::CreateWorktree) {
		CreateWorktree(std::string());
		return;
	}
	if (action == Action::ForgetMissingWorktrees) {
		Execute(PlanForgetMissingWorktrees(repository, fSnapshot));
		return;
	}
	const WorktreeInfo *selected = SelectedWorktree();
	if (selected == nullptr) {
		return;
	}
	const WorktreeInfo worktree = *selected;
	switch (action) {
		case Action::WorktreeOpenDiff:
			OpenDiff(worktree);
			break;
		case Action::WorktreeOpenFolder:
			OpenFolder(worktree.path);
			break;
		case Action::WorktreeSetBranch: {
			const std::vector<std::string> names = BranchNames();
			if (names.empty()) {
				ShowError("There are no branches");
				break;
			}
			Form form("Set branch of " + worktree.path, "OK");
			form.AddChoice("branch", "Branch:", names);
			AskForm(form, [this, worktree, names](const Form &result) {
				Execute(PlanSetWorktreeBranch(fRepository.get(), fSnapshot, worktree, names[result.Choice("branch")]));
			});
			break;
		}
		case Action::WorktreeSetCommit: {
			const std::string spec = worktree.commit.has_value() ? OidString(*worktree.commit) : std::string();
			PickCommitThen(spec, [this, worktree](const std::string &commit) {
				Execute(PlanSetWorktreeCommit(fRepository.get(), fSnapshot, worktree, commit));
			});
			break;
		}
		case Action::WorktreeCreateBranch: {
			Form form("Create branch here", "OK");
			form.AddText("name", "Branch name:");
			AskForm(form, [this, worktree](const Form &result) {
				Execute(PlanCreateBranchHere(fRepository.get(), fSnapshot, worktree, result.Text("name")));
			});
			break;
		}
		case Action::WorktreeUnstageAll:
			Execute(PlanUnstageAll(repository, worktree));
			break;
		case Action::WorktreeDiscard: {
			Form form("Discard changes in " + worktree.path, "OK");
			form.AddCheck("untracked", "Also delete untracked files", false);
			AskForm(form, [this, worktree](const Form &result) {
				Execute(PlanDiscardChanges(fRepository.get(), worktree, result.Checked("untracked")));
			});
			break;
		}
		case Action::WorktreeRemove: {
			const Plan plan = PlanRemoveWorktree(repository, worktree, false);
			if (!plan.blocked.empty() && worktree.HasChanges() && !worktree.locked) {
				Form form("Remove worktree " + worktree.path, "OK");
				form.AddLabel("The worktree has changes.")
					.AddCheck("force", "Remove it anyway and discard them", false);
				AskForm(form, [this, worktree](const Form &result) {
					if (result.Checked("force")) {
						Execute(PlanRemoveWorktree(fRepository.get(), worktree, true));
					}
				});
				break;
			}
			Execute(plan);
			break;
		}
		case Action::WorktreeProtect: {
			Form form("Protect " + worktree.path, "OK");
			form.AddText("reason", "Reason:", "")
				.AddLabel("A protected worktree is not removed or forgotten, and its files are not updated.");
			AskForm(form, [this, worktree](const Form &result) {
				Execute(PlanProtectWorktree(worktree, true, result.Text("reason")));
			});
			break;
		}
		case Action::WorktreeUnprotect:
			Execute(PlanProtectWorktree(worktree, false, std::string()));
			break;
		case Action::WorktreeAbort:
			Execute(PlanAbortOperation(repository, worktree));
			break;
		default:
			break;
	}
}


void MainWindow::UpdateFromTracked(const BranchInfo &branch)
{
	static const std::vector<std::string> kModes = {
		"Fast-forward only",
		"Merge the tracked branch into this",
		"Replay this branch's commits onto the tracked branch",
	};
	Form form("Update " + branch.name + " from " + branch.upstream, "Download");
	form.AddChoice("mode", "Integration:", kModes, 0)
		.AddLabel("Merge and replay run without a worktree; conflicts are not supported yet.");
	AskForm(form, [this, branch](const Form &result) {
		UpdateFromTracked(branch, static_cast<UpdateMode>(result.Choice("mode")));
	});
}


void MainWindow::UpdateFromTracked(const BranchInfo &branch, UpdateMode mode)
{
	const std::string remote = UpstreamRemote(fRepository.get(), branch.name);
	if (remote.empty()) {
		if (mode == UpdateMode::Replay) {
			RebaseBranch(branch.name, branch.upstream);
		} else {
			Execute(PlanUpdateFromTracked(fRepository.get(), fSnapshot, branch.name, mode));
		}
		return;
	}
	BMessage done(kMsgNetworkDone);
	done.AddInt32("followUp", static_cast<int32>(FollowUp::UpdateFromTracked));
	done.AddString("branch", branch.name.c_str());
	done.AddInt32("mode", static_cast<int32>(mode));
	StartNetwork("Download from " + remote, [remote](git_repository *repository, NetworkHandler &handler,
		std::vector<std::string> &report) {
		(void)report;
		Download(repository, remote, false, false, handler);
	}, done);
}


void MainWindow::CleanUpBranches()
{
	const std::vector<std::string> names = BranchNames();
	if (names.empty()) {
		return;
	}
	int defaultChoice = 0;
	for (size_t i = 0; i < names.size(); i++) {
		if (names[i] == "main" || names[i] == "master") {
			defaultChoice = static_cast<int>(i);
		}
	}
	Form form("Clean up branches", "Find branches");
	form.AddChoice("into", "Merged into:", names, defaultChoice)
		.AddLabel("Lists branches merged into this one, or whose tracked remote branch was deleted.");
	AskForm(form, [this, names](const Form &result) {
		const auto candidates = CleanupCandidates(fRepository.get(), fSnapshot, names[result.Choice("into")]);
		if (candidates.empty()) {
			ShowText(this, "Clean up branches", "No branch to clean up.");
			return;
		}
		std::vector<std::vector<std::string>> rows;
		for (const auto &[name, reason] : candidates) {
			rows.push_back({name, reason});
		}
		ChooseRowsThen("Select the branches to delete", "OK", {{"Branch", 200}, {"Reason", 360}}, rows, true,
			[this, candidates](const std::vector<int> &selected) {
				std::vector<std::string> deleted;
				for (int index : selected) {
					deleted.push_back(candidates[index].first);
				}
				Execute(PlanDeleteBranches(fRepository.get(), fSnapshot, deleted));
			});
	});
}


void MainWindow::ShowPreviousPositions(const BranchInfo &branch)
{
	git_repository *repository = fRepository.get();
	const std::vector<ReflogEntry> entries = PreviousPositions(repository, "refs/heads/" + branch.name);
	std::vector<std::vector<std::string>> rows;
	for (const ReflogEntry &entry : entries) {
		rows.push_back({FormatTime(entry.time), Describe(repository, entry.newId), entry.message});
	}
	const std::string name = branch.name;
	ChooseRowsThen("Previous positions of " + branch.name, "Set commit" B_UTF8_ELLIPSIS,
		{{"Date", 130}, {"Commit", 300}, {"Change", 260}}, rows, false,
		[this, name, entries](const std::vector<int> &selected) {
			Execute(PlanSetBranchCommit(fRepository.get(), fSnapshot, name, OidString(entries[selected[0]].newId)));
		});
}


void MainWindow::RunBranchAction(Action action)
{
	git_repository *repository = fRepository.get();
	switch (action) {
		case Action::CreateBranch: {
			Form form("Create branch", "OK");
			form.AddText("name", "Name:")
				.AddCommit("commit", "Start commit:", DefaultCommit())
				.AddText("upstream", "Tracks:", "")
				.AddChoice("worktree", "Then:", {"Do not set it in a worktree", "Set it in a new worktree"}, 0);
			AskForm(form, [this](const Form &result) {
				if (result.Choice("worktree") == 1) {
					// The worktree form creates the branch with the worktree.
					CreateWorktree(result.Text("name"));
					return;
				}
				Execute(PlanCreateBranch(fRepository.get(), fSnapshot, result.Text("name"), result.Text("commit"),
					result.Text("upstream")));
			}, true);
			return;
		}
		case Action::UpdateAllFromTracked:
			Execute(PlanUpdateAllFromTracked(repository, fSnapshot));
			return;
		case Action::CleanUpBranches:
			CleanUpBranches();
			return;
		default:
			break;
	}

	const BranchInfo *selected = SelectedBranch();
	if (selected == nullptr) {
		return;
	}
	const BranchInfo branch = *selected;
	switch (action) {
		case Action::BranchSetCommit: {
			PickCommitThen(branch.name, [this, branch](const std::string &spec) {
				Execute(PlanSetBranchCommit(fRepository.get(), fSnapshot, branch.name, spec));
			});
			break;
		}
		case Action::BranchUpdateFromTracked:
			if (branch.upstream.empty()) {
				ShowError("The branch does not track a remote branch");
				break;
			}
			UpdateFromTracked(branch);
			break;
		case Action::BranchMerge:
		case Action::BranchReplay:
		case Action::BranchRebaseInteractive: {
			const std::string start = branch.upstream.empty() || branch.upstreamGone ? std::string() : branch.upstream;
			PickCommitThen(start, [this, action, branch](const std::string &spec) {
				if (action == Action::BranchMerge) {
					Execute(PlanMergeInto(fRepository.get(), fSnapshot, branch.name, spec));
				} else if (action == Action::BranchReplay) {
					RebaseBranch(branch.name, spec);
				} else {
					ShowHistory(branch.name, true, spec);
				}
			});
			break;
		}
		case Action::BranchUpload: {
			const std::vector<std::string> remotes = RemoteNames();
			if (remotes.empty()) {
				ShowError("There are no remotes");
				break;
			}
			const std::string upstreamRemote = UpstreamRemote(repository, branch.name);
			int remoteChoice = 0;
			for (size_t i = 0; i < remotes.size(); i++) {
				if (remotes[i] == upstreamRemote) {
					remoteChoice = static_cast<int>(i);
				}
			}
			Form form("Upload " + branch.name, "OK");
			form.AddChoice("remote", "Remote:", remotes, remoteChoice)
				.AddChoice("mode", "Mode:", {
					"Normal (fast-forward only)",
					"Overwrite remote if unchanged since last download",
					"Overwrite remote unconditionally (advanced)",
				}, 0)
				.AddCheck("track", "Track the uploaded branch", branch.upstream.empty());
			AskForm(form, [this, branch, remotes](const Form &result) {
				Execute(PlanUploadBranch(fRepository.get(), fSnapshot, branch.name, remotes[result.Choice("remote")],
					static_cast<UploadMode>(result.Choice("mode")), result.Checked("track")));
			});
			break;
		}
		case Action::BranchUploadForReview:
			UploadForReview(branch);
			break;
		case Action::BranchSetUpstream: {
			std::vector<std::string> copies;
			for (const RemoteInfo &remote : fSnapshot.remotes) {
				for (const RemoteCopyInfo &copy : remote.copies) {
					copies.push_back(copy.name);
				}
			}
			if (copies.empty()) {
				ShowError("There are no remote copies; download from a remote first");
				break;
			}
			Form form("Set tracked remote branch of " + branch.name, "OK");
			form.AddChoice("upstream", "Track:", copies);
			AskForm(form, [this, branch, copies](const Form &result) {
				Execute(PlanSetUpstream(fRepository.get(), fSnapshot, branch.name, copies[result.Choice("upstream")]));
			});
			break;
		}
		case Action::BranchStopTracking:
			Execute(PlanSetUpstream(repository, fSnapshot, branch.name, std::string()));
			break;
		case Action::BranchAddTag: {
			Form form("Add tag at " + branch.name, "OK");
			form.AddText("name", "Tag name:")
				.AddMultiLine("message", "Message:")
				.AddLabel("Without a message, the tag is a simple tag.");
			AskForm(form, [this, branch](const Form &result) {
				Execute(PlanCreateTag(fRepository.get(), fSnapshot, result.Text("name"), branch.name,
					result.Text("message")));
			});
			break;
		}
		case Action::BranchSetInWorktree: {
			ChooseWorktree("Set " + branch.name + " in worktree", "OK", branch.worktree,
				[this, branch](const WorktreeInfo &worktree) {
					Execute(PlanSetWorktreeBranch(fRepository.get(), fSnapshot, worktree, branch.name));
				});
			break;
		}
		case Action::BranchNewWorktree:
			CreateWorktree(branch.name);
			break;
		case Action::BranchRename: {
			Form form("Rename " + branch.name, "OK");
			form.AddText("name", "New name:", branch.name);
			AskForm(form, [this, branch](const Form &result) {
				Execute(PlanRenameBranch(fRepository.get(), fSnapshot, branch.name, result.Text("name")));
			});
			break;
		}
		case Action::BranchDelete: {
			const std::string remote = UpstreamRemote(repository, branch.name);
			const bool onServer = !branch.upstream.empty() && !branch.upstreamGone && !remote.empty();
			const std::string serverBranch = branch.upstream.substr(std::min(remote.size() + 1, branch.upstream.size()));
			// Deleting on the server follows once the branch is deleted here.
			auto deleteBranch = [this, branch, remote, serverBranch](bool onServerToo) {
				std::function<void()> then;
				if (onServerToo) {
					then = [this, branch, remote, serverBranch]() {
						if (fSnapshot.FindBranch(branch.name) == nullptr) {
							Execute(PlanDeleteOnServer(fSnapshot, remote, "refs/heads/" + serverBranch));
						}
					};
				}
				Execute(PlanDeleteBranches(fRepository.get(), fSnapshot, {branch.name}), then);
			};
			if (!onServer) {
				deleteBranch(false);
				break;
			}
			Form form("Delete " + branch.name, "OK");
			form.AddCheck("server", "Also delete " + branch.upstream + " on the server", false);
			AskForm(form, [deleteBranch](const Form &result) {
				deleteBranch(result.Checked("server"));
			});
			break;
		}
		case Action::BranchPreviousPositions:
			ShowPreviousPositions(branch);
			break;
		case Action::BranchUndo: {
			const std::vector<ReflogEntry> entries = PreviousPositions(repository, "refs/heads/" + branch.name);
			if (entries.empty() || git_oid_is_zero(&entries[0].oldId)) {
				ShowError("There is no previous position");
				break;
			}
			Execute(PlanSetBranchCommit(repository, fSnapshot, branch.name, OidString(entries[0].oldId)));
			break;
		}
		case Action::BranchHistory:
			ShowHistory(branch.name, false);
			break;
		case Action::BranchEditHistory:
			ShowHistory(branch.name, true);
			break;
		default:
			break;
	}
}


void MainWindow::RunRemoteAction(Action action)
{
	switch (action) {
		case Action::AddRemote: {
			Form form("Add remote", "OK");
			form.AddText("name", "Name:", fSnapshot.remotes.empty() ? "origin" : "")
				.AddText("url", "Download URL:")
				.AddText("pushUrl", "Upload URL:")
				.AddLabel("Leave the upload URL empty to upload to the download URL.")
				.AddCheck("download", "Download now", true);
			AskForm(form, [this](const Form &result) {
				const std::string name = result.Text("name");
				std::function<void()> then;
				if (result.Checked("download")) {
					then = [this, name]() {
						if (fSnapshot.FindRemote(name) != nullptr) {
							Execute(PlanDownload(fSnapshot, name, false, false));
						}
					};
				}
				Execute(PlanAddRemote(fRepository.get(), fSnapshot, name, result.Text("url"), result.Text("pushUrl")),
					then);
			});
			return;
		}
		case Action::DownloadAll:
			Execute(PlanDownload(fSnapshot, std::string(), false, false));
			return;
		default:
			break;
	}

	if (const RemoteCopyInfo *selectedCopy = SelectedRemoteCopy()) {
		const RemoteCopyInfo copy = *selectedCopy;
		const std::string remote = copy.name.substr(0, copy.name.size() - copy.branch.size() - 1);
		switch (action) {
			case Action::CopyCreateBranch: {
				Form form("Create branch from " + copy.name, "OK");
				form.AddText("name", "Name:", copy.branch)
					.AddCheck("track", "Track " + copy.name, true);
				AskForm(form, [this, copy](const Form &result) {
					Execute(PlanCreateBranch(fRepository.get(), fSnapshot, result.Text("name"), copy.name,
						result.Checked("track") ? copy.name : std::string()));
				});
				break;
			}
			case Action::CopySetTracked: {
				const std::vector<std::string> names = BranchNames();
				if (names.empty()) {
					break;
				}
				Form form("Set " + copy.name + " as tracked", "OK");
				form.AddChoice("branch", "For branch:", names);
				AskForm(form, [this, copy, names](const Form &result) {
					Execute(PlanSetUpstream(fRepository.get(), fSnapshot, names[result.Choice("branch")], copy.name));
				});
				break;
			}
			case Action::CopySetWorktree: {
				ChooseWorktree("Set worktree to " + copy.name, "OK", std::string(),
					[this, copy](const WorktreeInfo &worktree) {
						Execute(PlanSetWorktreeCommit(fRepository.get(), fSnapshot, worktree, copy.name));
					});
				break;
			}
			case Action::CopyDeleteOnServer:
				Execute(PlanDeleteOnServer(fSnapshot, remote, "refs/heads/" + copy.branch));
				break;
			case Action::CopyHistory:
				ShowHistory(copy.name, false);
				break;
			default:
				break;
		}
		return;
	}

	const RemoteInfo *selected = SelectedRemote();
	if (selected == nullptr) {
		return;
	}
	const RemoteInfo remote = *selected;
	switch (action) {
		case Action::RemoteDownload: {
			Form form("Download from " + remote.name, "OK");
			form.AddCheck("prune", "Remove stale remote copies (branches deleted on the server)", false)
				.AddCheck("tags", "Download all tags", false);
			AskForm(form, [this, remote](const Form &result) {
				Execute(PlanDownload(fSnapshot, remote.name, result.Checked("prune"), result.Checked("tags")));
			});
			break;
		}
		case Action::RemoteDownloadChange:
			DownloadChange(remote);
			break;
		case Action::RemoteCheckServer: {
			BMessage done(kMsgNetworkDone);
			done.AddInt32("followUp", static_cast<int32>(FollowUp::Report));
			done.AddString("title", ("Differences with the server " + remote.name).c_str());
			const Snapshot snapshot = fSnapshot;
			const std::string name = remote.name;
			StartNetwork("Check the server " + remote.name, [snapshot, name](git_repository *repository,
				NetworkHandler &handler, std::vector<std::string> &report) {
				report = CompareWithServer(repository, snapshot, name, ListServer(repository, name, handler));
			}, done);
			break;
		}
		case Action::RemoteEdit: {
			Form form("Edit URLs of " + remote.name, "OK");
			form.AddText("url", "Download URL:", remote.url)
				.AddText("pushUrl", "Upload URL:", remote.pushUrl)
				.AddLabel("Leave the upload URL empty to upload to the download URL.");
			AskForm(form, [this, remote](const Form &result) {
				Execute(PlanEditRemote(fSnapshot, remote.name, result.Text("url"), result.Text("pushUrl")));
			});
			break;
		}
		case Action::RemoteRename: {
			Form form("Rename " + remote.name, "OK");
			form.AddText("name", "New name:", remote.name);
			AskForm(form, [this, remote](const Form &result) {
				Execute(PlanRenameRemote(fSnapshot, remote.name, result.Text("name")));
			});
			break;
		}
		case Action::RemoteRemove:
			Execute(PlanRemoveRemote(fSnapshot, remote.name));
			break;
		default:
			break;
	}
}


void MainWindow::UploadTags()
{
	ChooseRemote("Upload tags", std::string(), [this](const std::string &remote) {
		std::vector<std::string> tagNames;
		std::vector<std::vector<std::string>> rows;
		for (const TagInfo &tag : fSnapshot.tags) {
			tagNames.push_back(tag.name);
			rows.push_back({tag.name});
		}
		ChooseRowsThen("Select the tags to upload to " + remote, "OK", {{"Tag", 400}}, rows, true,
			[this, remote, tagNames](const std::vector<int> &selected) {
				std::vector<std::string> names;
				for (int index : selected) {
					names.push_back(tagNames[index]);
				}
				Execute(PlanUploadTags(fRepository.get(), fSnapshot, remote, names));
			});
	});
}


void MainWindow::RunTagAction(Action action)
{
	git_repository *repository = fRepository.get();
	switch (action) {
		case Action::CreateTag: {
			Form form("Create tag", "OK");
			form.AddText("name", "Name:")
				.AddCommit("commit", "Commit:", DefaultCommit())
				.AddMultiLine("message", "Message:")
				.AddLabel("Without a message, the tag is a simple tag.");
			AskForm(form, [this](const Form &result) {
				Execute(PlanCreateTag(fRepository.get(), fSnapshot, result.Text("name"), result.Text("commit"),
					result.Text("message")));
			}, true);
			return;
		}
		case Action::UploadTags:
			UploadTags();
			return;
		case Action::DownloadTags: {
			ChooseRemote("Download tags", std::string(), [this](const std::string &remote) {
				Execute(PlanDownload(fSnapshot, remote, false, true));
			});
			return;
		}
		default:
			break;
	}

	const TagInfo *selected = SelectedTag();
	if (selected == nullptr) {
		return;
	}
	const TagInfo tag = *LoadedTag(selected->name);
	const std::string spec = "tags/" + tag.name;
	switch (action) {
		case Action::TagMove: {
			PickCommitThen(spec, [this, tag](const std::string &commit) {
				Execute(PlanMoveTag(fRepository.get(), fSnapshot, tag.name, commit));
			});
			break;
		}
		case Action::TagEditMessage: {
			Form form("Edit message of " + tag.name, "OK");
			form.AddMultiLine("message", "Message:", tag.message)
				.AddLabel("Without a message, the tag becomes a simple tag.");
			AskForm(form, [this, tag](const Form &result) {
				Execute(PlanEditTagMessage(fRepository.get(), fSnapshot, tag.name, result.Text("message")));
			});
			break;
		}
		case Action::TagRename: {
			Form form("Rename " + tag.name, "OK");
			form.AddText("name", "New name:", tag.name);
			AskForm(form, [this, tag](const Form &result) {
				Execute(PlanRenameTag(fRepository.get(), fSnapshot, tag.name, result.Text("name")));
			});
			break;
		}
		case Action::TagUpload: {
			ChooseRemote("Upload " + tag.name, std::string(), [this, tag](const std::string &remote) {
				Execute(PlanUploadTags(fRepository.get(), fSnapshot, remote, {tag.name}));
			});
			break;
		}
		case Action::TagDelete:
			Execute(PlanDeleteTag(repository, fSnapshot, tag.name));
			break;
		case Action::TagDeleteOnServer: {
			ChooseRemote("Delete " + tag.name + " on the server", std::string(), [this, tag](const std::string &remote) {
				Execute(PlanDeleteOnServer(fSnapshot, remote, "refs/tags/" + tag.name));
			});
			break;
		}
		case Action::TagCreateBranch: {
			Form form("Create branch from " + tag.name, "OK");
			form.AddText("name", "Name:");
			AskForm(form, [this, spec](const Form &result) {
				Execute(PlanCreateBranch(fRepository.get(), fSnapshot, result.Text("name"), spec, std::string()));
			});
			break;
		}
		case Action::TagSetWorktree: {
			ChooseWorktree("Set worktree to " + tag.name, "OK", std::string(),
				[this, spec](const WorktreeInfo &worktree) {
					Execute(PlanSetWorktreeCommit(fRepository.get(), fSnapshot, worktree, spec));
				});
			break;
		}
		case Action::TagHistory:
			ShowHistory(spec, false);
			break;
		default:
			break;
	}
}


void MainWindow::OpenDiff(const WorktreeInfo &worktree)
{
	if (worktree.missing) {
		ShowError("The worktree folder is missing");
		return;
	}
	const auto existing = fDiffWindows.find(worktree.path);
	if (existing != fDiffWindows.end() && existing->second.LockTarget()) {
		BLooper *looper = nullptr;
		existing->second.Target(&looper);
		static_cast<BWindow*>(looper)->Activate();
		looper->Unlock();
		return;
	}
	std::unique_ptr<GitDiff::Repository> diffRepository;
	try {
		diffRepository = std::make_unique<GitDiff::Repository>(worktree.path);
	} catch (const std::exception &e) {
		ShowError(e.what());
		return;
	}
	GitDiff::DiffWindow *window = new GitDiff::DiffWindow(std::move(diffRepository), std::string());
	fDiffWindows[worktree.path] = BMessenger(window);
	window->Show();
}


//#pragma mark - Gerrit

void MainWindow::UploadForReview(const BranchInfo &branch)
{
	const std::vector<std::string> remotes = RemoteNames();
	if (remotes.empty()) {
		ShowError("There are no remotes");
		return;
	}
	// The tracked remote branch is the usual target; otherwise main or
	// master, as the remote has them.
	const std::string upstreamRemote = UpstreamRemote(fRepository.get(), branch.name);
	int remoteChoice = 0;
	for (size_t i = 0; i < remotes.size(); i++) {
		if (remotes[i] == upstreamRemote) {
			remoteChoice = static_cast<int>(i);
		}
	}
	const std::string &defaultRemote = remotes[remoteChoice];
	std::string target;
	if (!upstreamRemote.empty() && !branch.upstreamGone) {
		target = branch.upstream.substr(upstreamRemote.size() + 1);
	} else if (fSnapshot.FindRemoteCopy(defaultRemote + "/main") == nullptr
		&& fSnapshot.FindRemoteCopy(defaultRemote + "/master") != nullptr) {
		target = "master";
	} else {
		target = "main";
	}

	Form form("Upload " + branch.name + " for review", "OK");
	form.AddChoice("remote", "Remote:", remotes, remoteChoice)
		.AddText("target", "Target branch:", target)
		.AddText("topic", "Topic:")
		.AddChoice("state", "State:", {"As the server decides", "Work in progress", "Ready for review"}, 0)
		.AddText("reviewers", "Reviewers:")
		.AddText("cc", "CC:")
		.AddLabel("Separate names or e-mail addresses with spaces or commas.");
	AskForm(form, [this, branch, remotes](const Form &result) {
		ReviewOptions options;
		options.topic = result.Text("topic");
		options.state = static_cast<ReviewState>(result.Choice("state"));
		options.reviewers = SplitNames(result.Text("reviewers"));
		options.cc = SplitNames(result.Text("cc"));
		Execute(PlanUploadForReview(fRepository.get(), fSnapshot, branch.name, remotes[result.Choice("remote")],
			result.Text("target"), options));
	});
}


void MainWindow::DownloadChange(const RemoteInfo &remote)
{
	Form form("Download a review change from " + remote.name, "Download");
	form.AddText("change", "Change:")
		.AddText("patchset", "Patch set:")
		.AddLabel("A change number, number/patch set, or a link to the change.")
		.AddLabel("Without a patch set, the latest is downloaded.")
		.AddText("branch", "New branch:")
		.AddLabel("Optional; the change is kept as refs/review/" + remote.name + "/<change>/<patch set>.")
		.Derive("branch", "change", [](const std::string &text) {
			int change = 0;
			std::optional<int> patchset;
			return ParseChange(text, change, patchset) ? "review/" + std::to_string(change) : std::string();
		});
	// The form stays until the download works, so that a mistake can be
	// corrected.
	form.OpenWithAction(this, [this, remote](const Form &result, std::function<void(const std::string &error)> finished) {
		try {
			DownloadChange(remote, result, finished);
		} catch (const std::exception &e) {
			finished(e.what());
		}
	});
}


void MainWindow::DownloadChange(const RemoteInfo &remote, const Form &form,
	std::function<void(const std::string &error)> finished)
{
	int change = 0;
	std::optional<int> patchset;
	if (!ParseChange(form.Text("change"), change, patchset)) {
		finished("\"" + form.Text("change") + "\" is not a change number or a link to a change");
		return;
	}
	const std::string patchsetText = form.Text("patchset");
	if (!patchsetText.empty()) {
		int number = 0;
		std::optional<int> parsed;
		if (!ParseChange(patchsetText, number, parsed) || parsed.has_value()) {
			finished("\"" + patchsetText + "\" is not a patch set number");
			return;
		}
		patchset = number;
	}
	if (fNetworkRunning) {
		finished("Another network operation is running; try again when it ends");
		return;
	}
	const std::string branch = form.Text("branch");
	const Plan plan = PlanDownloadChange(fSnapshot, remote.name, change, patchset, branch);
	if (!plan.blocked.empty()) {
		finished(plan.blocked);
		return;
	}
	BMessage done(kMsgNetworkDone);
	done.AddInt32("followUp", static_cast<int32>(FollowUp::ShowChange));
	done.AddString("remote", remote.name.c_str());
	done.AddInt32("change", change);
	done.AddInt32("patchset", patchset.value_or(0));
	done.AddString("branch", branch.c_str());
	// Downloading loses nothing: it runs without a preview.
	fNetworkFinished = std::move(finished);
	RunPlan(plan, Resolution::Cancel, done, nullptr);
}


void MainWindow::ShowHistory(const std::string &spec, bool edit, const std::string &onto)
{
	try {
		HistoryWindow *window = new HistoryWindow(fRepositoryPath, spec, edit, onto);
		DiffKit::PlaceOver(window, Frame());
		window->Show();
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
}


void MainWindow::RebaseBranch(const std::string &branch, const std::string &onto)
{
	const git_oid commit = ResolveCommit(fRepository.get(), onto);
	// Replayed on a thread: it takes seconds in large repositories.
	struct Outcome {
		bool changes {};
		std::optional<Plan> plan;
	};
	std::shared_ptr<Outcome> outcome = std::make_shared<Outcome>();
	const std::string path = fRepositoryPath;
	const Snapshot snapshot = fSnapshot;
	DiffKit::RunWithProgress(this, "Replaying " + branch + " onto " + onto,
		[path, branch, commit, snapshot, outcome](DiffKit::Progress &progress) {
			RepositoryPtr repository = OpenRepository(path);
			HistoryEdit edit(repository.get(), branch, commit, 1000, true);
			outcome->changes = edit.HasChanges();
			if (!outcome->changes) {
				return;
			}
			edit.SetReplay(edit.Revision(), edit.ComputeReplay(repository.get(), [&progress](size_t done,
				size_t count) {
				progress.SetText("Replaying commit " + std::to_string(done + 1) + " of " + std::to_string(count));
			}));
			for (const StepResult &result : edit.Results()) {
				if (result.state == StepState::Conflict) {
					return;
				}
			}
			outcome->plan = edit.PlanApply(snapshot);
		}, [this, outcome, branch, onto](const std::string &error) {
			if (!error.empty()) {
				ShowError(error.c_str());
			} else if (!outcome->changes) {
				ShowError(("Branch " + branch + " is already based on " + onto).c_str());
			} else if (outcome->plan.has_value()) {
				Plan plan = *outcome->plan;
				plan.title = "Replay branch " + branch + " onto " + onto;
				Execute(plan);
			} else {
				AskToResolveRebase(branch, onto);
			}
		});
}


void MainWindow::AskToResolveRebase(const std::string &branch, const std::string &onto)
{
	BAlert *alert = new BAlert("Git", ("Replaying " + branch + " onto " + onto
		+ " has conflicts. Resolve them in the history editor?").c_str(), "Cancel", "Open the editor", nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	DiffKit::Ask(this, alert, [this, branch, onto](int32 button) {
		if (button == 1) {
			ShowHistory(branch, true, onto);
		}
	});
}


void MainWindow::OpenFolder(const std::string &path)
{
	entry_ref ref;
	if (get_ref_for_path(path.c_str(), &ref) != B_OK) {
		ShowError(("Cannot open " + path).c_str());
		return;
	}
	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &ref);
	BMessenger("application/x-vnd.Be-TRAK").SendMessage(&message);
}


//#pragma mark - Messages

void MainWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgOpenRepository:
			be_app->PostMessage(message->what);
			break;
		case kMsgQuitApplication:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		case kMsgRefresh:
		case kMsgRefreshTimer:
			Refresh();
			break;
		case kMsgSnapshotLoaded:
			SnapshotLoaded();
			break;
		case kMsgLoadFirst:
			LoadFirst();
			break;
		case kMsgTagFilter:
			if (fTagFilter != nullptr && fTagFilterText != fTagFilter->Text()) {
				fTagFilterText = fTagFilter->Text();
				FillObjects();
				ShowDetails();
			}
			break;
		case B_NODE_MONITOR:
			ScheduleRefresh();
			break;
		case kMsgCategorySelected: {
			const int32 index = fCategories->CurrentSelection();
			if (index >= 0 && static_cast<Category>(index) != fCategory) {
				fCategory = static_cast<Category>(index);
				ShowCategoryButtons();
				FillObjects();
				ShowDetails();
			}
			break;
		}
		case kMsgObjectSelected:
			ShowDetails();
			break;
		case kMsgObjectInvoked:
			ObjectInvoked();
			break;
		case kMsgContextMenu:
			ShowContextMenu();
			break;
		case kMsgAction:
			RunAction(static_cast<Action>(message->GetInt32("action", 0)));
			break;
		case kMsgNetworkLog:
			AppendLog(message->GetString("text", ""));
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void MainWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	if (active) {
		ScheduleRefresh();
	}
}


// During a network operation, the window stays open and asks; the answer
// closes it, or asks the application to quit again if the request came from
// there.
bool MainWindow::QuitRequested()
{
	if (fNetworkRunning && !fCloseConfirmed) {
		if (!fAskingClose) {
			fAskingClose = true;
			// The application asks its windows from its own thread.
			const bool fromApplication = find_thread(nullptr) != Thread();
			BAlert *alert = new BAlert("Git", "A network operation is running. Cancel it and close?", "Keep running",
				"Cancel and close", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			DiffKit::Ask(this, alert, [this, fromApplication](int32 button) {
				fAskingClose = false;
				if (button != 1) {
					return;
				}
				fCloseConfirmed = true;
				if (fromApplication) {
					be_app->PostMessage(B_QUIT_REQUESTED);
				} else {
					PostMessage(B_QUIT_REQUESTED);
				}
			});
		}
		return false;
	}
	// Closing the window closes the progress dialog, which cancels the work.
	BMessage closed(kMsgMainWindowClosed);
	closed.AddString("path", fRepositoryPath.c_str());
	be_app->PostMessage(&closed);
	return true;
}


} // GitTool
