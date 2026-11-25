#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Window.h>

#include "HistoryEdit.h"
#include "Snapshot.h"
#include "StatsLoader.h"
#include "StepEditor.h"


class BButton;
class BCheckBox;
class BList;
class BMenuField;
class BRow;
class BStringView;
class BTextControl;


namespace GitTool {


class CommitList;
class CommitRow;


// The history of a branch, remote copy, tag or commit. In view mode with
// merges as trees, the list shows the first-parent line, and each merge
// expands to the commits it brings in (read when first expanded; under one
// row per parent for an octopus merge), nested for the merges among them.
// In edit mode (local branches) the commits can be reordered by drag and
// drop, removed, combined, reworded, edited, split and their conflicts
// resolved; commits dropped from another history window are copied in.
// Apply rewrites the branch once. Has its own repository handle.
class HistoryWindow: public BWindow {
private:
	// The branches of a merge, read on a thread.
	struct MergeBranches {
		std::vector<MergedBranch> branches;
		std::string error;
	};
	struct BranchesLoad {
		uint64_t generation {};
		std::string merge;
		MergeBranches result;
	};
	// Where the threads put what they read.
	struct BranchesInbox {
		std::mutex lock;
		std::vector<BranchesLoad> loads;
	};

	std::string fRepositoryPath;
	RepositoryPtr fRepository;
	Snapshot fSnapshot;
	// The shown history, in revision syntax.
	std::string fSpec;
	// Counts the histories read, so that what is read for an older one is
	// dropped.
	uint64_t fGeneration {};
	// View mode rows: the first-parent line with merges as trees, else every
	// commit.
	bool fTree = true;
	std::vector<CommitInfo> fCommits;
	// Read branches of merges, by OidString of the merge.
	std::map<std::string, MergeBranches> fBranches;
	std::set<std::string> fLoadingBranches;
	std::shared_ptr<BranchesInbox> fInbox = std::make_shared<BranchesInbox>();
	// The expanded rows by RowKey, kept when the list is filled again.
	std::set<std::string> fExpanded;
	std::unique_ptr<HistoryEdit> fEdit;
	std::unique_ptr<StatsLoader> fStats;
	// Branches, remote copies and tags by commit (ReferenceLabels).
	std::map<std::string, std::vector<std::string>> fLabels;

	// What is read on a thread: the history, and with it on opening the
	// snapshot and the labels.
	struct Loaded {
		uint64_t generation {};
		std::string spec;
		std::vector<CommitInfo> commits;
		std::string historyError;
		std::optional<Snapshot> snapshot;
		std::map<std::string, std::vector<std::string>> labels;
	};
	// Asked for when opening; done once the first history is shown.
	bool fPendingEdit {};
	// Closing asks about pending edits without blocking.
	bool fAskingClose {};
	bool fCloseConfirmed {};
	std::string fPendingOnto;

	BMenuField *fRefField {};
	BTextControl *fSpecField {};
	BCheckBox *fEditMode {};
	BCheckBox *fTreeMode {};
	CommitList *fList {};
	BStringView *fStatus {};
	BButton *fShowChanges {};
	BButton *fRemove {};
	BButton *fCombine {};
	BButton *fReword {};
	BButton *fEditContents {};
	BButton *fSplit {};
	BButton *fResolve {};
	BButton *fRebase {};
	BButton *fReset {};
	BButton *fApply {};

	// Reads the snapshot (without worktree change counts) and labels on a
	// thread, then runs then (for operations); a read error is shown first.
	void ReloadThen(std::function<void()> then);
	// Shows the history of spec once it is read; withSnapshot reads the
	// snapshot and labels too.
	void ShowHistoryOf(const std::string &spec, bool withSnapshot = false);
	void LoadHistory(const std::string &spec, bool withSnapshot);
	void HistoryLoaded(Loaded loaded);
	void FillList();
	// A row of a commit, not added yet.
	CommitRow *MakeRow(const git_oid &commit, int32 step, const std::string &state, const std::string &summary,
		const git_oid &statsCommit, const CommitInfo *known = nullptr);
	// Adds rows under parent (none: at the top), the first with AddRow:
	// AddRows needs a parent that has rows already.
	void AddChildRows(BList &rows, BRow *parent);
	// Adds the commits of one line of the tree under parent, with what is
	// read of the branches of the merges among them; a note for none.
	void AddLine(BRow *parent, const std::vector<CommitInfo> &commits, const std::vector<git_oid> &hidden,
		bool truncated);
	// The rows under a merge: its branches if read, else a placeholder.
	void AddMergeRows(CommitRow *row);
	// The key of a row in fExpanded; empty for a row that is not kept.
	std::string RowKey(BRow *row) const;
	void RowExpanded(BRow *row, bool expanded);
	// Reads the branches of a merge on a thread, once.
	void LoadBranches(const git_oid &merge, const std::vector<git_oid> &hidden);
	void BranchesLoaded();
	void ForEachRow(const std::function<void(BRow *row)> &function, BRow *parent = nullptr) const;
	void UpdateControls();
	std::vector<size_t> SelectedSteps() const;
	std::vector<git_oid> SelectedCommits() const;
	// Runs discard at once without pending edits, else after the user agrees
	// to discard them; keep runs if the user does not.
	void ConfirmDiscard(std::function<void()> discard, std::function<void()> keep = nullptr);
	void SetEditMode(bool edit);
	// Builds the edits with create on a thread, with a repository handle of
	// its own, and replays them there, under a progress dialog; then done
	// runs with whether they replaced the current edits.
	void StartEditing(
		const std::string &title,
		std::function<std::unique_ptr<HistoryEdit>(git_repository *repository)> create,
		std::function<void(bool started)> done
	);
	// Shows the edits after a change, replaying them first under a progress
	// dialog.
	void ReplayEdits();
	void Dropped(BMessage *message);
	void Remove();
	void Combine();
	void Reword();
	void OpenStepEditor(StepEditMode mode);
	void StepEdited(BMessage *message);
	void Apply();
	// Replaces the edits by a rebase of the branch onto the commit.
	void StartRebase(const std::string &onto);
	void StartRebaseNow(const std::string &onto);
	void ChooseRebase();
	void ShowChanges();
	void StatsReady(BMessage *message);
	// The selected commit that exists as it is shown (not a pending
	// rewrite), for references and copying.
	std::optional<git_oid> SelectedExistingCommit() const;
	void ShowContextMenu(BPoint where);
	void CreateReferenceAt(bool tag);
	void CopyCommitIds();

public:
	// edit starts in edit mode if spec is a local branch; with onto (a
	// commit in revision syntax) it starts as a rebase onto it.
	HistoryWindow(const std::string &repositoryPath, const std::string &spec, bool edit,
		const std::string &onto = std::string());

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // GitTool
