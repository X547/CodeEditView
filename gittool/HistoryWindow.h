#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Window.h>

#include "BackgroundLoader.h"
#include "HistoryEdit.h"
#include "Snapshot.h"
#include "StatsLoader.h"
#include "StepEditor.h"


class BButton;
class BCheckBox;
class BMenuField;
class BStringView;
class BTextControl;


namespace GitTool {


class CommitList;


// The history of a branch, remote copy, tag or commit. In edit mode (local
// branches) the commits can be reordered by drag and drop, removed,
// combined, reworded, edited, split and their conflicts resolved; commits
// dropped from another history window are copied in. Apply rewrites the
// branch once. Has its own repository handle.
class HistoryWindow: public BWindow {
private:
	std::string fRepositoryPath;
	RepositoryPtr fRepository;
	Snapshot fSnapshot;
	// The shown history, in revision syntax.
	std::string fSpec;
	// View mode rows.
	std::vector<CommitInfo> fCommits;
	std::unique_ptr<HistoryEdit> fEdit;
	std::unique_ptr<StatsLoader> fStats;
	// Branches, remote copies and tags by commit (ReferenceLabels).
	std::map<std::string, std::vector<std::string>> fLabels;

	// What is read on a thread: the history, and with it on opening the
	// snapshot and the labels.
	struct Loaded {
		std::string spec;
		std::vector<CommitInfo> commits;
		std::string historyError;
		std::optional<Snapshot> snapshot;
		std::map<std::string, std::vector<std::string>> labels;
	};
	std::unique_ptr<DiffKit::BackgroundLoader<Loaded>> fLoader;
	// Asked for when opening; done once the first history is shown.
	bool fPendingEdit {};
	// Closing asks about pending edits without blocking.
	bool fAskingClose {};
	bool fCloseConfirmed {};
	std::string fPendingOnto;

	BMenuField *fRefField {};
	BTextControl *fSpecField {};
	BCheckBox *fEditMode {};
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

	// Reads the snapshot and labels now (for operations).
	void Reload();
	// Shows the history of spec once it is read; withSnapshot reads the
	// snapshot and labels too.
	void ShowHistoryOf(const std::string &spec, bool withSnapshot = false);
	void LoadHistory(const std::string &spec, bool withSnapshot);
	void HistoryLoaded();
	void FillList();
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
