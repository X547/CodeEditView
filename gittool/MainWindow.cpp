#include "MainWindow.h"

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
#include <TextView.h>

#include "DiffView.h"
#include "DiffWindow.h"
#include "Dialogs.h"
#include "HistoryWindow.h"
#include "Messages.h"
#include "Operations.h"
#include "Repository.h"


namespace GitTool {


using DiffKit::ShowError;


// Delay before refreshing after a change on disk; changes come in bursts.
static constexpr bigtime_t kRefreshDelay = 300000;


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
	BranchUpload,
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
	kSeparator,
	{Action::BranchUpload, "Upload" ELLIPSIS},
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
		BRect(60, 60, 1059, 719),
		"Git",
		B_DOCUMENT_WINDOW,
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

	fButtons = new BGroupView(B_HORIZONTAL, B_USE_SMALL_SPACING);
	ShowCategoryButtons();

	fDetails = new BTextView("details");
	fDetails->MakeEditable(false);
	fDetails->SetWordWrap(true);
	fLog = new BTextView("log");
	fLog->MakeEditable(false);
	fLog->SetWordWrap(true);
	BScrollView *detailScroll = new BScrollView("detailScroll", fDetails, 0, false, true);
	detailScroll->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 130));
	BScrollView *logScroll = new BScrollView("logScroll", fLog, 0, false, true);
	logScroll->SetExplicitPreferredSize(BSize(B_SIZE_UNSET, 70));
	BScrollView *categoryScroll = new BScrollView("categoryScroll", fCategories, 0, false, false);
	categoryScroll->SetExplicitPreferredSize(BSize(170, B_SIZE_UNSET));
	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fCancelButton = new BButton("Cancel", new BMessage(kMsgCancelNetwork));
	fCancelButton->SetEnabled(false);

	// Extra space goes to the object list; the category list, details and
	// log keep their size unless the user moves a splitter.
	BSplitView *rightSplit = new BSplitView(B_VERTICAL, B_USE_SMALL_SPACING);
	BLayoutBuilder::Split<>(rightSplit)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 1)
			.Add(fButtons)
			.Add(fObjects)
		.End()
		.Add(detailScroll, 0)
		.Add(logScroll, 0);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
			.SetInsets(B_USE_SMALL_SPACING)
			.AddSplit(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(categoryScroll, 0)
				.Add(rightSplit, 1)
			.End()
			.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
				.Add(fStatus)
				.Add(fCancelButton)
			.End()
		.End();

	Watch();
	Refresh();
	fCategories->Select(0);
	UpdateTitle();
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
	bool sameTags = false;
	try {
		Snapshot snapshot = Snapshot::Load(fRepository.get());
		// Unchanged tags keep their details, and their rows are not rebuilt.
		sameTags = SameTags(fSnapshot.tags, snapshot.tags);
		if (sameTags) {
			snapshot.tags = std::move(fSnapshot.tags);
		}
		fSnapshot = std::move(snapshot);
	} catch (const std::exception &e) {
		AppendLog(std::string("Cannot read the repository: ") + e.what());
	}
	FillCategories();
	if (fCategory != Category::Tags || !sameTags) {
		FillObjects();
	}
	ShowDetails();
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
		item->SetText((std::string(kNames[i]) + " (" + std::to_string(counts[i]) + ")").c_str());
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

	auto addRow = [this](ObjectKind kind, const std::string &name, const std::vector<std::string> &fields,
		BRow *parent = nullptr) {
		ObjectRow *row = new ObjectRow(kind, name);
		for (size_t i = 0; i < fields.size(); i++) {
			row->SetField(new BStringField(fields[i].c_str()), static_cast<int32>(i));
		}
		fObjects->AddRow(row, parent);
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
				BRow *parent = addRow(ObjectKind::Remote, remote.name, {remote.name, remote.url});
				for (const RemoteCopyInfo &copy : remote.copies) {
					addRow(ObjectKind::RemoteCopy, copy.name, {copy.name, ShortId(copy.commit) + " " + copy.summary},
						parent);
				}
				fObjects->ExpandOrCollapse(parent, true);
			}
			break;
		case Category::Tags:
			for (size_t i = 0; i < fSnapshot.tags.size(); i++) {
				ObjectRow *row = new ObjectRow(ObjectKind::Tag, fSnapshot.tags[i].name);
				row->SetField(new BStringField(fSnapshot.tags[i].name.c_str()), 0);
				for (int32 column = 1; column <= 3; column++) {
					row->SetField(new LazyField([this, i, column]() {
						return TagText(i, column);
					}), column);
				}
				fObjects->AddRow(row);
			}
			break;
	}

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

void MainWindow::Execute(const Plan &plan)
{
	Resolution resolution = Resolution::Cancel;
	if (!ConfirmPlan(plan, fRepository.get(), resolution)) {
		return;
	}
	if (plan.network) {
		StartNetwork(plan.title, [plan, resolution](git_repository *repository, NetworkHandler &handler,
			std::vector<std::string> &report) {
			(void)report;
			RunContext context;
			context.repository = repository;
			context.resolution = resolution;
			context.network = &handler;
			plan.run(context);
		}, BMessage(kMsgNetworkDone));
		return;
	}
	RunContext context;
	context.repository = fRepository.get();
	context.resolution = resolution;
	try {
		plan.run(context);
		AppendLog("Done: " + plan.title);
	} catch (const std::exception &e) {
		AppendLog("Failed: " + plan.title + ": " + e.what());
		ShowError(e.what());
	}
	Refresh();
}


void MainWindow::StartNetwork(const std::string &status, NetworkTask::Work work, const BMessage &done)
{
	if (fTask != nullptr) {
		ShowError("Another network operation is running; wait for it or cancel it first");
		return;
	}
	BMessage message(done);
	if (!message.HasString("title")) {
		message.AddString("title", status.c_str());
	}
	fTask = std::make_unique<NetworkTask>(BMessenger(this), fRepositoryPath, std::move(work), message);
	fStatus->SetText((status + B_UTF8_ELLIPSIS).c_str());
	fCancelButton->SetEnabled(true);
	AppendLog(status);
	fTask->Start();
}


void MainWindow::NetworkDone(BMessage *message)
{
	std::vector<std::string> report;
	if (fTask != nullptr) {
		report = fTask->Report();
		fTask.reset();
	}
	fCancelButton->SetEnabled(false);
	const std::string title = message->GetString("title", "");
	const std::string error = message->GetString("error", "");
	if (!error.empty()) {
		fStatus->SetText(("Failed: " + title).c_str());
		AppendLog("Failed: " + title + ": " + error);
		Refresh();
		ShowError(error.c_str());
		return;
	}
	fStatus->SetText(("Done: " + title).c_str());
	AppendLog("Done: " + title);
	Refresh();

	switch (static_cast<FollowUp>(message->GetInt32("followUp", 0))) {
		case FollowUp::UpdateFromTracked: {
			const std::string branch = message->GetString("branch", "");
			const UpdateMode mode = static_cast<UpdateMode>(message->GetInt32("mode", 0));
			try {
				Execute(PlanUpdateFromTracked(fRepository.get(), fSnapshot, branch, mode));
			} catch (const std::exception &e) {
				ShowError(e.what());
			}
			break;
		}
		case FollowUp::Report: {
			std::string text;
			for (const std::string &line : report) {
				text += line + "\n";
			}
			ShowText(title, text.empty() ? "The local copies match the server." : text);
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


bool MainWindow::ChooseWorktree(const std::string &title, const std::string &okLabel, const std::string &exceptPath,
	WorktreeInfo &worktree)
{
	std::vector<const WorktreeInfo*> candidates;
	std::vector<std::string> labels;
	for (const WorktreeInfo &candidate : fSnapshot.worktrees) {
		if (candidate.path != exceptPath && !candidate.missing) {
			candidates.push_back(&candidate);
			labels.push_back(candidate.path + " (" + PositionText(candidate) + ")");
		}
	}
	if (candidates.empty()) {
		ShowError("There is no other worktree");
		return false;
	}
	Form form(title, okLabel);
	form.AddChoice("worktree", "Worktree:", labels);
	if (!form.Go()) {
		return false;
	}
	worktree = *candidates[form.Choice("worktree")];
	return true;
}


bool MainWindow::ChooseRemote(const std::string &title, const std::string &okLabel, std::string &remote)
{
	const std::vector<std::string> names = RemoteNames();
	if (names.empty()) {
		ShowError("There are no remotes");
		return false;
	}
	if (names.size() == 1 && okLabel.empty()) {
		remote = names[0];
		return true;
	}
	Form form(title, okLabel.empty() ? "OK" : okLabel);
	form.AddChoice("remote", "Remote:", names);
	if (!form.Go()) {
		return false;
	}
	remote = names[form.Choice("remote")];
	return true;
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
	Form form("Create worktree", "Preview");
	form.AddText("path", "Folder:", folder + branch)
		.AddChoice("start", "Start:", kStarts, 0)
		.AddText("branch", "Branch:", branch)
		.AddCommit("commit", "Start commit:", branch.empty() ? DefaultCommit() : branch)
		.AddText("upstream", "New branch tracks:", "")
		.AddLabel("A new branch starts at the start commit; \"tracks\" is an optional remote copy, as origin/main.");
	if (!form.Go(fRepository.get(), &fSnapshot)) {
		return;
	}
	WorktreeStart start;
	start.kind = static_cast<WorktreeStartKind>(form.Choice("start"));
	start.branch = form.Text("branch");
	start.commit = form.Text("commit");
	start.upstream = form.Text("upstream");
	Execute(PlanCreateWorktree(fRepository.get(), fSnapshot, form.Text("path"), start));
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
			Form form("Set branch of " + worktree.path, "Preview");
			form.AddChoice("branch", "Branch:", names);
			if (form.Go()) {
				Execute(PlanSetWorktreeBranch(repository, fSnapshot, worktree, names[form.Choice("branch")]));
			}
			break;
		}
		case Action::WorktreeSetCommit: {
			std::string spec = worktree.commit.has_value() ? OidString(*worktree.commit) : std::string();
			if (PickCommit(repository, fSnapshot, spec)) {
				Execute(PlanSetWorktreeCommit(repository, fSnapshot, worktree, spec));
			}
			break;
		}
		case Action::WorktreeCreateBranch: {
			Form form("Create branch here", "Preview");
			form.AddText("name", "Branch name:");
			if (form.Go()) {
				Execute(PlanCreateBranchHere(repository, fSnapshot, worktree, form.Text("name")));
			}
			break;
		}
		case Action::WorktreeUnstageAll:
			Execute(PlanUnstageAll(repository, worktree));
			break;
		case Action::WorktreeDiscard: {
			Form form("Discard changes in " + worktree.path, "Preview");
			form.AddCheck("untracked", "Also delete untracked files", false);
			if (form.Go()) {
				Execute(PlanDiscardChanges(repository, worktree, form.Checked("untracked")));
			}
			break;
		}
		case Action::WorktreeRemove: {
			Plan plan = PlanRemoveWorktree(repository, worktree, false);
			if (!plan.blocked.empty() && worktree.HasChanges() && !worktree.locked) {
				Form form("Remove worktree " + worktree.path, "Preview");
				form.AddLabel("The worktree has changes.")
					.AddCheck("force", "Remove it anyway and discard them", false);
				if (!form.Go() || !form.Checked("force")) {
					break;
				}
				plan = PlanRemoveWorktree(repository, worktree, true);
			}
			Execute(plan);
			break;
		}
		case Action::WorktreeProtect: {
			Form form("Protect " + worktree.path, "Preview");
			form.AddText("reason", "Reason:", "")
				.AddLabel("A protected worktree is not removed or forgotten, and its files are not updated.");
			if (form.Go()) {
				Execute(PlanProtectWorktree(worktree, true, form.Text("reason")));
			}
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
	Form form("Update " + branch.name + " from " + branch.upstream, "Download and preview");
	form.AddChoice("mode", "Integration:", kModes, 0)
		.AddLabel("Merge and replay run without a worktree; conflicts are not supported yet.");
	if (!form.Go()) {
		return;
	}
	const UpdateMode mode = static_cast<UpdateMode>(form.Choice("mode"));
	const std::string remote = UpstreamRemote(fRepository.get(), branch.name);
	if (remote.empty()) {
		Execute(PlanUpdateFromTracked(fRepository.get(), fSnapshot, branch.name, mode));
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
	if (!form.Go()) {
		return;
	}
	const auto candidates = CleanupCandidates(fRepository.get(), fSnapshot, names[form.Choice("into")]);
	if (candidates.empty()) {
		ShowText("Clean up branches", "No branch to clean up.");
		return;
	}
	std::vector<std::vector<std::string>> rows;
	for (const auto &[name, reason] : candidates) {
		rows.push_back({name, reason});
	}
	std::vector<int> selected;
	if (!ChooseFromList("Select the branches to delete", "Preview", {{"Branch", 200}, {"Reason", 360}}, rows, true,
		selected)) {
		return;
	}
	std::vector<std::string> deleted;
	for (int index : selected) {
		deleted.push_back(candidates[index].first);
	}
	Execute(PlanDeleteBranches(fRepository.get(), fSnapshot, deleted));
}


void MainWindow::ShowPreviousPositions(const BranchInfo &branch)
{
	git_repository *repository = fRepository.get();
	const std::vector<ReflogEntry> entries = PreviousPositions(repository, "refs/heads/" + branch.name);
	std::vector<std::vector<std::string>> rows;
	for (const ReflogEntry &entry : entries) {
		rows.push_back({FormatTime(entry.time), Describe(repository, entry.newId), entry.message});
	}
	std::vector<int> selected;
	if (!ChooseFromList("Previous positions of " + branch.name, "Set commit" B_UTF8_ELLIPSIS,
		{{"Date", 130}, {"Commit", 300}, {"Change", 260}}, rows, false, selected)) {
		return;
	}
	Execute(PlanSetBranchCommit(repository, fSnapshot, branch.name, OidString(entries[selected[0]].newId)));
}


void MainWindow::RunBranchAction(Action action)
{
	git_repository *repository = fRepository.get();
	switch (action) {
		case Action::CreateBranch: {
			Form form("Create branch", "Preview");
			form.AddText("name", "Name:")
				.AddCommit("commit", "Start commit:", DefaultCommit())
				.AddText("upstream", "Tracks:", "")
				.AddChoice("worktree", "Then:", {"Do not set it in a worktree", "Set it in a new worktree"}, 0);
			if (!form.Go(repository, &fSnapshot)) {
				return;
			}
			if (form.Choice("worktree") == 1) {
				// The worktree form creates the branch with the worktree.
				CreateWorktree(form.Text("name"));
				return;
			}
			Execute(PlanCreateBranch(repository, fSnapshot, form.Text("name"), form.Text("commit"),
				form.Text("upstream")));
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
			std::string spec = branch.name;
			if (PickCommit(repository, fSnapshot, spec)) {
				Execute(PlanSetBranchCommit(repository, fSnapshot, branch.name, spec));
			}
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
		case Action::BranchReplay: {
			std::string spec = branch.upstream.empty() || branch.upstreamGone ? std::string() : branch.upstream;
			if (!PickCommit(repository, fSnapshot, spec)) {
				break;
			}
			Execute(action == Action::BranchMerge ? PlanMergeInto(repository, fSnapshot, branch.name, spec)
				: PlanReplayOnto(repository, fSnapshot, branch.name, spec));
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
			Form form("Upload " + branch.name, "Preview");
			form.AddChoice("remote", "Remote:", remotes, remoteChoice)
				.AddChoice("mode", "Mode:", {
					"Normal (fast-forward only)",
					"Overwrite remote if unchanged since last download",
					"Overwrite remote unconditionally (advanced)",
				}, 0)
				.AddCheck("track", "Track the uploaded branch", branch.upstream.empty());
			if (form.Go()) {
				Execute(PlanUploadBranch(repository, fSnapshot, branch.name, remotes[form.Choice("remote")],
					static_cast<UploadMode>(form.Choice("mode")), form.Checked("track")));
			}
			break;
		}
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
			Form form("Set tracked remote branch of " + branch.name, "Preview");
			form.AddChoice("upstream", "Track:", copies);
			if (form.Go()) {
				Execute(PlanSetUpstream(repository, fSnapshot, branch.name, copies[form.Choice("upstream")]));
			}
			break;
		}
		case Action::BranchStopTracking:
			Execute(PlanSetUpstream(repository, fSnapshot, branch.name, std::string()));
			break;
		case Action::BranchAddTag: {
			Form form("Add tag at " + branch.name, "Preview");
			form.AddText("name", "Tag name:")
				.AddMultiLine("message", "Message:")
				.AddLabel("Without a message, the tag is a simple tag.");
			if (form.Go()) {
				Execute(PlanCreateTag(repository, fSnapshot, form.Text("name"), branch.name, form.Text("message")));
			}
			break;
		}
		case Action::BranchSetInWorktree: {
			WorktreeInfo worktree;
			if (ChooseWorktree("Set " + branch.name + " in worktree", "Preview", branch.worktree, worktree)) {
				Execute(PlanSetWorktreeBranch(repository, fSnapshot, worktree, branch.name));
			}
			break;
		}
		case Action::BranchNewWorktree:
			CreateWorktree(branch.name);
			break;
		case Action::BranchRename: {
			Form form("Rename " + branch.name, "Preview");
			form.AddText("name", "New name:", branch.name);
			if (form.Go()) {
				Execute(PlanRenameBranch(repository, fSnapshot, branch.name, form.Text("name")));
			}
			break;
		}
		case Action::BranchDelete: {
			const bool onServer = !branch.upstream.empty() && !branch.upstreamGone
				&& !UpstreamRemote(repository, branch.name).empty();
			bool deleteOnServer = false;
			if (onServer) {
				Form form("Delete " + branch.name, "Preview");
				form.AddCheck("server", "Also delete " + branch.upstream + " on the server", false);
				if (!form.Go()) {
					break;
				}
				deleteOnServer = form.Checked("server");
			}
			const std::string remote = UpstreamRemote(repository, branch.name);
			const std::string serverBranch = branch.upstream.substr(std::min(remote.size() + 1, branch.upstream.size()));
			Execute(PlanDeleteBranches(repository, fSnapshot, {branch.name}));
			if (deleteOnServer && fSnapshot.FindBranch(branch.name) == nullptr) {
				Execute(PlanDeleteOnServer(fSnapshot, remote, "refs/heads/" + serverBranch));
			}
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
	git_repository *repository = fRepository.get();
	switch (action) {
		case Action::AddRemote: {
			Form form("Add remote", "Preview");
			form.AddText("name", "Name:", fSnapshot.remotes.empty() ? "origin" : "")
				.AddText("url", "Download URL:")
				.AddText("pushUrl", "Upload URL:")
				.AddLabel("Leave the upload URL empty to upload to the download URL.")
				.AddCheck("download", "Download now", true);
			if (!form.Go()) {
				return;
			}
			const std::string name = form.Text("name");
			Execute(PlanAddRemote(repository, fSnapshot, name, form.Text("url"), form.Text("pushUrl")));
			if (form.Checked("download") && fSnapshot.FindRemote(name) != nullptr) {
				Execute(PlanDownload(fSnapshot, name, false, false));
			}
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
				Form form("Create branch from " + copy.name, "Preview");
				form.AddText("name", "Name:", copy.branch)
					.AddCheck("track", "Track " + copy.name, true);
				if (form.Go()) {
					Execute(PlanCreateBranch(repository, fSnapshot, form.Text("name"), copy.name,
						form.Checked("track") ? copy.name : std::string()));
				}
				break;
			}
			case Action::CopySetTracked: {
				const std::vector<std::string> names = BranchNames();
				if (names.empty()) {
					break;
				}
				Form form("Set " + copy.name + " as tracked", "Preview");
				form.AddChoice("branch", "For branch:", names);
				if (form.Go()) {
					Execute(PlanSetUpstream(repository, fSnapshot, names[form.Choice("branch")], copy.name));
				}
				break;
			}
			case Action::CopySetWorktree: {
				WorktreeInfo worktree;
				if (ChooseWorktree("Set worktree to " + copy.name, "Preview", std::string(), worktree)) {
					Execute(PlanSetWorktreeCommit(repository, fSnapshot, worktree, copy.name));
				}
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
			Form form("Download from " + remote.name, "Preview");
			form.AddCheck("prune", "Remove stale remote copies (branches deleted on the server)", false)
				.AddCheck("tags", "Download all tags", false);
			if (form.Go()) {
				Execute(PlanDownload(fSnapshot, remote.name, form.Checked("prune"), form.Checked("tags")));
			}
			break;
		}
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
			Form form("Edit URLs of " + remote.name, "Preview");
			form.AddText("url", "Download URL:", remote.url)
				.AddText("pushUrl", "Upload URL:", remote.pushUrl)
				.AddLabel("Leave the upload URL empty to upload to the download URL.");
			if (form.Go()) {
				Execute(PlanEditRemote(fSnapshot, remote.name, form.Text("url"), form.Text("pushUrl")));
			}
			break;
		}
		case Action::RemoteRename: {
			Form form("Rename " + remote.name, "Preview");
			form.AddText("name", "New name:", remote.name);
			if (form.Go()) {
				Execute(PlanRenameRemote(fSnapshot, remote.name, form.Text("name")));
			}
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
	git_repository *repository = fRepository.get();
	std::string remote;
	if (!ChooseRemote("Upload tags", std::string(), remote)) {
		return;
	}
	std::vector<std::vector<std::string>> rows;
	for (const TagInfo &tag : fSnapshot.tags) {
		rows.push_back({tag.name});
	}
	std::vector<int> selected;
	if (!ChooseFromList("Select the tags to upload to " + remote, "Preview", {{"Tag", 400}}, rows, true, selected)) {
		return;
	}
	std::vector<std::string> names;
	for (int index : selected) {
		names.push_back(fSnapshot.tags[index].name);
	}
	Execute(PlanUploadTags(repository, fSnapshot, remote, names));
}


void MainWindow::RunTagAction(Action action)
{
	git_repository *repository = fRepository.get();
	switch (action) {
		case Action::CreateTag: {
			Form form("Create tag", "Preview");
			form.AddText("name", "Name:")
				.AddCommit("commit", "Commit:", DefaultCommit())
				.AddMultiLine("message", "Message:")
				.AddLabel("Without a message, the tag is a simple tag.");
			if (form.Go(repository, &fSnapshot)) {
				Execute(PlanCreateTag(repository, fSnapshot, form.Text("name"), form.Text("commit"),
					form.Text("message")));
			}
			return;
		}
		case Action::UploadTags:
			UploadTags();
			return;
		case Action::DownloadTags: {
			std::string remote;
			if (ChooseRemote("Download tags", std::string(), remote)) {
				Execute(PlanDownload(fSnapshot, remote, false, true));
			}
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
			std::string commit = spec;
			if (PickCommit(repository, fSnapshot, commit)) {
				Execute(PlanMoveTag(repository, fSnapshot, tag.name, commit));
			}
			break;
		}
		case Action::TagEditMessage: {
			Form form("Edit message of " + tag.name, "Preview");
			form.AddMultiLine("message", "Message:", tag.message)
				.AddLabel("Without a message, the tag becomes a simple tag.");
			if (form.Go()) {
				Execute(PlanEditTagMessage(repository, fSnapshot, tag.name, form.Text("message")));
			}
			break;
		}
		case Action::TagRename: {
			Form form("Rename " + tag.name, "Preview");
			form.AddText("name", "New name:", tag.name);
			if (form.Go()) {
				Execute(PlanRenameTag(repository, fSnapshot, tag.name, form.Text("name")));
			}
			break;
		}
		case Action::TagUpload: {
			std::string remote;
			if (ChooseRemote("Upload " + tag.name, std::string(), remote)) {
				Execute(PlanUploadTags(repository, fSnapshot, remote, {tag.name}));
			}
			break;
		}
		case Action::TagDelete:
			Execute(PlanDeleteTag(repository, fSnapshot, tag.name));
			break;
		case Action::TagDeleteOnServer: {
			std::string remote;
			if (ChooseRemote("Delete " + tag.name + " on the server", std::string(), remote)) {
				Execute(PlanDeleteOnServer(fSnapshot, remote, "refs/tags/" + tag.name));
			}
			break;
		}
		case Action::TagCreateBranch: {
			Form form("Create branch from " + tag.name, "Preview");
			form.AddText("name", "Name:");
			if (form.Go()) {
				Execute(PlanCreateBranch(repository, fSnapshot, form.Text("name"), spec, std::string()));
			}
			break;
		}
		case Action::TagSetWorktree: {
			WorktreeInfo worktree;
			if (ChooseWorktree("Set worktree to " + tag.name, "Preview", std::string(), worktree)) {
				Execute(PlanSetWorktreeCommit(repository, fSnapshot, worktree, spec));
			}
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


void MainWindow::ShowHistory(const std::string &spec, bool edit)
{
	try {
		(new HistoryWindow(fRepositoryPath, spec, edit))->Show();
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
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
		case kMsgCancelNetwork:
			if (fTask != nullptr) {
				fTask->Cancel();
				fStatus->SetText("Cancelling" B_UTF8_ELLIPSIS);
			}
			break;
		case kMsgNetworkProgress:
			fStatus->SetText(message->GetString("text", ""));
			break;
		case kMsgNetworkLog:
			AppendLog(message->GetString("text", ""));
			break;
		case kMsgNetworkDone:
			NetworkDone(message);
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


bool MainWindow::QuitRequested()
{
	if (fTask != nullptr) {
		BAlert *alert = new BAlert("Git", "A network operation is running. Cancel it and close?", "Keep running",
			"Cancel and close", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		if (alert->Go() != 1) {
			return false;
		}
		fTask->Cancel();
		fTask.reset();
	}
	BMessage closed(kMsgMainWindowClosed);
	closed.AddString("path", fRepositoryPath.c_str());
	be_app->PostMessage(&closed);
	return true;
}


} // GitTool
