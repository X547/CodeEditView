#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include <Referenceable.h>
#include <Window.h>

#include "Comparison.h"
#include "FilePair.h"
#include "Scanner.h"


class BMenuBar;
class BMenuItem;
class CodeEditView;

namespace DiffKit {
class DiffView;
}


namespace DirCompare {


class DirTreeView;


// Two directories as a tree of differences and a diff of the selected file,
// or two files as a diff only. Both sides can be edited and saved.
class CompareWindow: public BWindow {
private:
	// Closing asks about modified files without blocking.
	bool fAskingClose {};
	bool fCloseConfirmed {};
	std::string fRoots[2];
	// Comparing two files: no tree.
	bool fFileMode {};
	ScanOptions fOptions;
	std::unique_ptr<Comparison> fComparison;
	// A scan runs under a progress dialog.
	bool fScanning {};
	// A scan completed: the comparison is of the roots.
	bool fScanned {};
	DirTreeView *fTree {};
	DiffKit::DiffView *fDiffView {};
	// Loaded file pairs by relative path; kept while shown or modified.
	std::map<std::string, BReference<FilePair>> fPairs;
	// The shown pair and its path.
	BReference<FilePair> fPair;
	std::string fPairPath;
	BMenuItem *fShowIdenticalItem {};
	BMenuItem *fIgnoreVcsItem {};
	BMenuItem *fSyncScrollingItem {};
	BMenuItem *fHideUnchangedItem {};
	BMenuItem *fWordWrapItem {};
	BMenuItem *fAlgorithmItems[3] {};

	BMenuBar *CreateMenuBar();
	std::string FullPath(int pane, const std::string &path) const;
	void StartScan();
	// Compares two roots on a thread of its own under a progress dialog;
	// they and the options become the window's when it completes.
	void StartScan(const std::string &left, const std::string &right, const ScanOptions &options);
	void ShowComparison(
		const std::string &left,
		const std::string &right,
		const ScanOptions &options,
		std::unique_ptr<Comparison> comparison
	);
	void Recheck(const std::string &path);
	FilePair *PairFor(const std::string &path);
	void ShowSelected();
	void ShowPair(FilePair *pair, const std::string &path);
	void UpdateDiffState();
	void UpdateTitle();
	bool HasModifiedPairs() const;
	void Save(bool all);
	void Reload();
	void CopyEntry(int toPane);
	void DeleteEntry(int pane);
	void ShowInTracker(int pane);
	void SwapSides();
	void ReloadPairsUnder(const std::string &path);
	// Runs then at once without modified files, else after the user saves or
	// discards them.
	void ConfirmDiscard(const char *question, std::function<void()> then);
	void SwapSidesNow();
	void CopyEntryNow(int fromPane, int toPane, const std::string &path);
	void DeleteEntryNow(int pane, const std::string &path);
	// Of the editor commands: the focused pane, else the right one.
	CodeEditView *TargetEditor();

public:
	// Compares two directories, or two files.
	CompareWindow(const std::string &left, const std::string &right);
	~CompareWindow() override;

	void MenusBeginning() override;
	void MessageReceived(BMessage *message) override;
	void WindowActivated(bool active) override;
	bool QuitRequested() override;
};


} // DirCompare
