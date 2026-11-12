#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Node.h>
#include <Referenceable.h>
#include <Window.h>

#include "BackgroundLoader.h"
#include "DiffView.h"
#include "FileDocument.h"
#include "Repository.h"


class BButton;
class BCheckBox;
class BMessageRunner;
class CodeEditView;


namespace GitDiff {


class ChangeView;


// The changed files of one repository and a diff of the selected one.
class DiffWindow: public BWindow {
private:
	std::unique_ptr<Repository> fRepository;
	// Documents that were shown, by path; kept while listed or modified.
	std::map<std::string, BReference<FileDocument>> fDocuments;
	// Files chosen in the path menu; listed until the window closes.
	std::set<std::string> fOpened;
	// The status is read on a thread; files to select wait for it. Each read
	// has a number; an older status than the one shown is not shown.
	std::unique_ptr<DiffKit::BackgroundLoader<std::pair<uint64, std::vector<StatusEntry>>>> fStatusLoader;
	uint64 fStatusRequest {};
	uint64 fShownRequest {};
	bool fFirstStatus = true;
	std::string fInitialPath;
	std::optional<std::string> fPendingSelect;
	ChangeView *fChangeView {};
	DiffView *fDiffView {};
	// The document and diff shown in fDiffView.
	BReference<FileDocument> fDocument;
	DiffKind fKind = DiffKind::Unstaged;
	std::unique_ptr<BMessageRunner> fRefreshRunner;
	node_ref fGitDirNode {};
	// The commit message, in fChangeView.
	CodeEditView *fMessage {};
	BCheckBox *fAmend {};
	BCheckBox *fChangeId {};
	// Closing asks about modified files without blocking: the answer closes
	// the window (or quits the application, if it asked).
	bool fAskingClose {};
	bool fCloseConfirmed {};

	FileDocument *DocumentFor(const std::string &path);
	void UpdateDiffState();
	bool CanEdit() const;
	void ChangesCopied(int pane);
	// The version of the shown file in a pane, at its caret: the working
	// tree's file, or HEAD's as a Git file (the index has none).
	void OpenInCodeEdit(int pane);
	void ShowSelectedFile();
	// Lists a file in "Other files" unless it has unstaged changes, and
	// shows it.
	void ShowPath(const std::string &path);
	// Reads the status on a thread; StatusLoaded() updates the window.
	void Refresh();
	// The first status is read under a progress dialog.
	void LoadFirst();
	void StatusLoaded();
	void ShowStatus(uint64 request, const std::vector<StatusEntry> &status);
	void ScheduleRefresh();
	void UpdateTitle();
	void Save(bool all);
	void ReloadDocument();
	void StageFile();
	void AmendToggled();
	void UpdateChangeId();
	void ChangeIdToggled();
	// Commits the index, asking first to write edited staged versions.
	void Commit();
	void CommitNow(bool writeIndex);
	bool HasModifiedDocuments() const;

public:
	// Takes the repository; shows the file at relativePath if not empty.
	DiffWindow(std::unique_ptr<Repository> repository, const std::string &relativePath);
	~DiffWindow() override;

	void MessageReceived(BMessage *message) override;
	void WindowActivated(bool active) override;
	bool QuitRequested() override;
};


} // GitDiff
