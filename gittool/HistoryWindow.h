#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Window.h>

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
	BButton *fReset {};
	BButton *fApply {};

	void Reload();
	void ShowHistoryOf(const std::string &spec);
	void FillList();
	void UpdateControls();
	std::vector<size_t> SelectedSteps() const;
	std::vector<git_oid> SelectedCommits() const;
	bool ConfirmDiscard();
	void SetEditMode(bool edit);
	void Dropped(BMessage *message);
	void Remove();
	void Combine();
	void Reword();
	void OpenStepEditor(StepEditMode mode);
	void StepEdited(BMessage *message);
	void Apply();
	void ShowChanges();
	void StatsReady(BMessage *message);
	// The selected commit that exists as it is shown (not a pending
	// rewrite), for references and copying.
	std::optional<git_oid> SelectedExistingCommit() const;
	void ShowContextMenu(BPoint where);
	void CreateReferenceAt(bool tag);
	void CopyCommitIds();

public:
	// edit starts in edit mode if spec is a local branch.
	HistoryWindow(const std::string &repositoryPath, const std::string &spec, bool edit);

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // GitTool
