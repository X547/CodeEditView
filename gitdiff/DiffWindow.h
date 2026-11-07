#pragma once

#include <map>
#include <memory>
#include <string>

#include <Node.h>
#include <Referenceable.h>
#include <Window.h>

#include "DiffView.h"
#include "FileDocument.h"
#include "Repository.h"


class BButton;
class BCheckBox;
class BMenuItem;
class BMessageRunner;
class BTextView;


namespace GitDiff {


class FileListView;


// The changed files of one repository and a diff of the selected one.
class DiffWindow: public BWindow {
private:
	std::unique_ptr<Repository> fRepository;
	// Documents that were shown, by path; kept while listed or modified.
	std::map<std::string, BReference<FileDocument>> fDocuments;
	FileListView *fFileList {};
	DiffView *fDiffView {};
	// The document and diff shown in fDiffView.
	BReference<FileDocument> fDocument;
	DiffKind fKind = DiffKind::Unstaged;
	std::unique_ptr<BMessageRunner> fRefreshRunner;
	BMenuItem *fSyncScrollingItem {};
	BMenuItem *fHideUnchangedItem {};
	BMenuItem *fWordWrapItem {};
	BMenuItem *fAlgorithmItems[3] {};
	node_ref fGitDirNode {};
	BTextView *fMessage {};
	BCheckBox *fAmend {};

	FileDocument *DocumentFor(const std::string &path);
	void UpdateDiffState();
	bool CanEdit() const;
	void ChangesCopied(int pane);
	void ShowSelectedFile();
	void Refresh();
	void ScheduleRefresh();
	void UpdateTitle();
	void Save(bool all);
	void ReloadDocument();
	void StageFile();
	void AmendToggled();
	void Commit();
	bool ConfirmClose();

public:
	// Takes the repository; shows the file at relativePath if not empty.
	DiffWindow(std::unique_ptr<Repository> repository, const std::string &relativePath);
	~DiffWindow() override;

	void MessageReceived(BMessage *message) override;
	void WindowActivated(bool active) override;
	bool QuitRequested() override;
};


} // GitDiff
