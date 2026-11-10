#include "Ask.h"
#include "CompareWindow.h"

#include <stdio.h>

#include <algorithm>
#include <exception>

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>
#include <Roster.h>
#include <SplitView.h>

#include <CodeEditView/CodeEditView.h>

#include <DiffView.h>

#include "DirTreeView.h"
#include "FileOps.h"
#include "Messages.h"


namespace DirCompare {


using DiffKit::DiffAlgorithm;
using DiffKit::DiffView;
using DiffKit::ShowError;


static const char *kSideNames[] = {"left", "right"};


static std::string Leaf(const std::string &path)
{
	return BPath(path.c_str()).Leaf() != nullptr ? BPath(path.c_str()).Leaf() : path;
}


static bool IsUnder(const std::string &path, const std::string &directory)
{
	return directory.empty() || path == directory
		|| (path.size() > directory.size() && path.compare(0, directory.size(), directory) == 0
			&& path[directory.size()] == '/');
}


static BMenuItem *Item(const char *label, uint32 what, char shortcut = 0, uint32 modifiers = 0, int32 pane = -1)
{
	BMessage *message = new BMessage(what);
	if (pane >= 0) {
		message->AddInt32("pane", pane);
	}
	return new BMenuItem(label, message, shortcut, modifiers);
}


namespace {

// Collects the results of a synchronous rescan.
class CollectingSink: public ScanSink {
public:
	std::vector<ScanEntry> entries;

	void EntriesFound(std::vector<ScanEntry> found) override
	{
		for (ScanEntry &entry : found) {
			entries.push_back(std::move(entry));
		}
	}

	void StatusesChanged(std::vector<StatusChange> changes) override
	{
		for (const StatusChange &change : changes) {
			for (ScanEntry &entry : entries) {
				if (entry.path == change.first) {
					entry.status = change.second;
				}
			}
		}
	}
};

}


CompareWindow::CompareWindow(const std::string &left, const std::string &right):
	BWindow(
		BRect(60, 60, 1379, 859),
		"Compare",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRoots {left, right}
{
	fFileMode = ReadEntryInfo(left).type != EntryType::Directory;
	BMenuBar *menuBar = CreateMenuBar();
	fDiffView = new DiffView();

	BGroupLayout *content = nullptr;
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frames overlap the window borders, as in gitdiff.
			.SetInsets(-1, 0, -1, -1)
			.GetLayout(&content)
		.End();
	if (fFileMode) {
		content->AddView(fDiffView);
		BReference<FilePair> pair(new FilePair(left, right), true);
		pair->Load();
		fPairs[""] = pair;
		ShowPair(pair.Get(), "");
	} else {
		fTree = new DirTreeView();
		BSplitView *splitView = new BSplitView(B_HORIZONTAL, 0);
		content->AddView(splitView);
		splitView->SetInsets(0, -1, 0, 0);
		splitView->AddChild(fTree, 2);
		splitView->AddChild(fDiffView, 3);
		StartScan();
	}
	UpdateTitle();
}


CompareWindow::~CompareWindow()
{
	fScan.reset();
	if (fTree != nullptr) {
		fTree->SetComparison(nullptr);
	}
}


BMenuBar *CompareWindow::CreateMenuBar()
{
	BMenuBar *menuBar = new BMenuBar("menuBar");

	BMenu *file = new BMenu("File");
	file->AddItem(Item("New comparison" B_UTF8_ELLIPSIS, kMsgNewComparison, 'N'));
	file->AddItem(Item("Refresh", kMsgRefresh, 'R', B_SHIFT_KEY));
	file->AddSeparatorItem();
	file->AddItem(Item("Save", kMsgSave, 'S'));
	file->AddItem(Item("Save all", kMsgSaveAll, 'S', B_SHIFT_KEY));
	file->AddItem(Item("Reload from disk", kMsgReload));
	file->AddSeparatorItem();
	file->AddItem(Item("Close", B_QUIT_REQUESTED, 'W'));
	file->AddItem(Item("Quit", kMsgQuitApplication, 'Q'));
	menuBar->AddItem(file);

	BMenu *edit = new BMenu("Edit");
	edit->AddItem(Item("Undo", B_UNDO, 'Z'));
	edit->AddItem(Item("Redo", B_REDO, 'Z', B_SHIFT_KEY));
	edit->AddSeparatorItem();
	edit->AddItem(Item("Cut", B_CUT, 'X'));
	edit->AddItem(Item("Copy", B_COPY, 'C'));
	edit->AddItem(Item("Paste", B_PASTE, 'V'));
	edit->AddSeparatorItem();
	edit->AddItem(Item("Select all", B_SELECT_ALL, 'A'));
	menuBar->AddItem(edit);

	BMenu *changes = new BMenu("Changes");
	changes->AddItem(Item("Copy change to left", kMsgCopyChanges, B_LEFT_ARROW, B_OPTION_KEY, 0));
	changes->AddItem(Item("Copy change to right", kMsgCopyChanges, B_RIGHT_ARROW, B_OPTION_KEY, 1));
	changes->AddSeparatorItem();
	changes->AddItem(Item("Next change", kMsgNextChange, B_DOWN_ARROW, B_SHIFT_KEY));
	changes->AddItem(Item("Previous change", kMsgPreviousChange, B_UP_ARROW, B_SHIFT_KEY));
	if (!fFileMode) {
		changes->AddSeparatorItem();
		changes->AddItem(Item("Next different file", kMsgNextFile, B_DOWN_ARROW, B_OPTION_KEY));
		changes->AddItem(Item("Previous different file", kMsgPreviousFile, B_UP_ARROW, B_OPTION_KEY));
	}
	menuBar->AddItem(changes);

	if (!fFileMode) {
		BMenu *files = new BMenu("Files");
		files->AddItem(Item("Copy to left side", kMsgCopyEntry, 0, 0, 0));
		files->AddItem(Item("Copy to right side", kMsgCopyEntry, 0, 0, 1));
		files->AddSeparatorItem();
		files->AddItem(Item("Delete from left side", kMsgDeleteEntry, 0, 0, 0));
		files->AddItem(Item("Delete from right side", kMsgDeleteEntry, 0, 0, 1));
		files->AddSeparatorItem();
		files->AddItem(Item("Show left in Tracker", kMsgShowInTracker, 0, 0, 0));
		files->AddItem(Item("Show right in Tracker", kMsgShowInTracker, 0, 0, 1));
		menuBar->AddItem(files);
	}

	BMenu *view = new BMenu("View");
	if (!fFileMode) {
		view->AddItem(fShowIdenticalItem = Item("Show identical files", kMsgToggleShowIdentical));
		view->AddItem(fIgnoreVcsItem = Item("Ignore version control directories", kMsgToggleIgnoreVcs));
		fIgnoreVcsItem->SetMarked(fOptions.ignoreVcs);
		view->AddSeparatorItem();
	}
	view->AddItem(fSyncScrollingItem = Item("Synchronize scrolling", kMsgToggleSyncScrolling));
	view->AddItem(fHideUnchangedItem = Item("Hide unchanged lines", kMsgToggleHideUnchanged));
	view->AddItem(fWordWrapItem = Item("Word wrap", kMsgToggleWordWrap));
	fSyncScrollingItem->SetMarked(true);
	fHideUnchangedItem->SetMarked(true);
	view->AddSeparatorItem();
	BMenu *algorithmMenu = new BMenu("Diff algorithm");
	static const char *kAlgorithmNames[] = {"Myers", "Minimal", "Patience"};
	for (int i = 0; i < 3; i++) {
		BMessage *message = new BMessage(kMsgSetAlgorithm);
		message->AddInt32("algorithm", i);
		fAlgorithmItems[i] = new BMenuItem(kAlgorithmNames[i], message);
		algorithmMenu->AddItem(fAlgorithmItems[i]);
	}
	algorithmMenu->SetRadioMode(true);
	fAlgorithmItems[0]->SetMarked(true);
	view->AddItem(algorithmMenu);
	view->AddSeparatorItem();
	view->AddItem(Item("Swap sides", kMsgSwapSides));
	menuBar->AddItem(view);
	return menuBar;
}


std::string CompareWindow::FullPath(int pane, const std::string &path) const
{
	return path.empty() ? fRoots[pane] : fRoots[pane] + "/" + path;
}


void CompareWindow::StartScan()
{
	if (fFileMode) {
		return;
	}
	if (!fReselect.has_value()) {
		fReselect = fTree->SelectedPath();
	}
	fScan.reset();
	fTree->SetComparison(nullptr);
	fComparison = std::make_unique<Comparison>();
	fTree->SetComparison(fComparison.get());
	fScan = std::make_unique<ScanThread>(fRoots[0], fRoots[1], fOptions, BMessenger(this));
	UpdateTitle();
}


void CompareWindow::TakeScanResults()
{
	if (fScan == nullptr) {
		return;
	}
	ScanThread::Results results = fScan->TakeResults();
	const std::vector<Node*> added = fComparison->AddEntries(results.entries);
	fComparison->ApplyChanges(results.changes);
	if (results.finished) {
		fComparison->SetComplete();
	}
	const std::vector<Node*> changed = fComparison->UpdateStatuses();
	fTree->Update(added, changed);
	if (fReselect.has_value() && fTree->SelectPath(*fReselect)) {
		fReselect.reset();
	}
	if (results.finished) {
		fScan.reset();
		fReselect.reset();
		UpdateTitle();
		if (!results.error.empty()) {
			ShowError(results.error.c_str());
		}
	}
}


// Compares an entry again after it was saved, copied or deleted.
void CompareWindow::Recheck(const std::string &path)
{
	if (fFileMode) {
		return;
	}
	// A running scan would report the old state; the root is all of it.
	if (fScan != nullptr || path.empty()) {
		StartScan();
		return;
	}
	ScanEntry self;
	self.path = path;
	for (int pane = 0; pane < 2; pane++) {
		self.sides[pane] = ReadEntryInfo(FullPath(pane, path));
	}
	self.status = InitialStatus(self.sides[0], self.sides[1]);
	if (self.status == EntryStatus::Pending) {
		self.status = CompareFiles(FullPath(0, path), FullPath(1, path));
	}
	CollectingSink sink;
	try {
		const std::atomic<bool> cancel(false);
		Scan(fRoots[0], fRoots[1], fOptions, sink, cancel, path);
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	const bool gone = self.sides[0].type == EntryType::None && self.sides[1].type == EntryType::None;
	fComparison->ReplaceSubtree(path, gone ? std::nullopt : std::optional<ScanEntry>(self), sink.entries);
	fComparison->UpdateStatuses();
	fTree->Rebuild();
}


FilePair *CompareWindow::PairFor(const std::string &path)
{
	const auto it = fPairs.find(path);
	if (it != fPairs.end()) {
		it->second->Load();
		return it->second.Get();
	}
	BReference<FilePair> pair(new FilePair(FullPath(0, path), FullPath(1, path)), true);
	pair->Load();
	fPairs[path] = pair;
	return pair.Get();
}


void CompareWindow::ShowSelected()
{
	const std::optional<std::string> path = fTree->SelectedPath();
	const Node *node = path.has_value() ? fComparison->Find(*path) : nullptr;
	if (node == nullptr || node->IsDirectory()) {
		ShowPair(nullptr, path.value_or(std::string()));
		return;
	}
	ShowPair(PairFor(*path), *path);
}


void CompareWindow::ShowPair(FilePair *pair, const std::string &path)
{
	fPair.SetTo(pair);
	fPairPath = path;
	// Unmodified pairs that are not shown are loaded again when needed.
	for (auto it = fPairs.begin(); it != fPairs.end();) {
		FilePair *other = it->second.Get();
		const bool keep = other == pair || other->IsModified(0) || other->IsModified(1);
		it = keep ? std::next(it) : fPairs.erase(it);
	}
	UpdateDiffState();
	fDiffView->SetModels(pair != nullptr ? pair->Model(0) : nullptr, pair != nullptr ? pair->Model(1) : nullptr,
		path);
	UpdateTitle();
}


void CompareWindow::UpdateDiffState()
{
	if (fPair == nullptr) {
		for (int pane = 0; pane < 2; pane++) {
			const std::string text = fPairPath.empty() ? std::string() : Leaf(fRoots[pane]) + "/" + fPairPath;
			fDiffView->SetLabel(pane, text.c_str());
		}
		fDiffView->SetCopyTargets(false, false);
		return;
	}
	for (int pane = 0; pane < 2; pane++) {
		std::string text = fFileMode ? fRoots[pane] : Leaf(fRoots[pane]) + "/" + fPairPath;
		if (!fPair->Exists(pane)) {
			text += " — missing";
		}
		if (fPair->IsModified(pane)) {
			text += " — modified";
		}
		if (fPair->ChangedOnDisk(pane)) {
			text += " — changed on disk";
		}
		if (!fPair->Problem(pane).empty()) {
			text += " — read-only: " + fPair->Problem(pane);
		}
		fDiffView->SetLabel(pane, text.c_str());
		fDiffView->SetReadOnly(pane, !fPair->Problem(pane).empty());
	}
	const bool text = fPair->Problem(0).empty() && fPair->Problem(1).empty();
	fDiffView->SetCopyTargets(text, text);
}


void CompareWindow::UpdateTitle()
{
	std::string title = Leaf(fRoots[0]) + " ↔ " + Leaf(fRoots[1]);
	if (fScan != nullptr) {
		title += " — comparing" B_UTF8_ELLIPSIS;
	}
	SetTitle(title.c_str());
}


bool CompareWindow::HasModifiedPairs() const
{
	for (const auto &[path, pair] : fPairs) {
		if (pair->IsModified(0) || pair->IsModified(1)) {
			return true;
		}
	}
	return false;
}


void CompareWindow::Save(bool all)
{
	std::vector<std::string> saved;
	for (const auto &[path, pair] : fPairs) {
		if (!all && pair != fPair) {
			continue;
		}
		for (int pane = 0; pane < 2; pane++) {
			if (!pair->IsModified(pane)) {
				continue;
			}
			try {
				pair->Save(pane);
				saved.push_back(path);
			} catch (const std::exception &e) {
				ShowError(e.what());
			}
		}
	}
	UpdateDiffState();
	// A new file may have created directories: recheck from the topmost
	// one that was missing.
	for (const std::string &path : saved) {
		const Node *node = fComparison != nullptr ? fComparison->Find(path) : nullptr;
		while (node != nullptr && node->parent != nullptr && node->parent->parent != nullptr
			&& (node->parent->sides[0].type == EntryType::None || node->parent->sides[1].type == EntryType::None)) {
			node = node->parent;
		}
		if (node != nullptr) {
			Recheck(std::string(node->path));
		}
	}
}


void CompareWindow::Reload()
{
	if (fPair == nullptr) {
		return;
	}
	if (fPair->IsModified(0) || fPair->IsModified(1)) {
		BAlert *alert = new BAlert("Reload", "Discard the changes to this file and load it from disk?",
			"Cancel", "Discard", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		alert->SetShortcut(0, B_ESCAPE);
		DiffKit::Ask(this, alert, [this, pair = fPair](int32 button) {
			// Another file may be shown meanwhile.
			if (button == 1 && fPair == pair) {
				fPair->Load(true);
				UpdateDiffState();
			}
		});
		return;
	}
	fPair->Load(true);
	UpdateDiffState();
}


void CompareWindow::ReloadPairsUnder(const std::string &path)
{
	for (const auto &[pairPath, pair] : fPairs) {
		if (IsUnder(pairPath, path)) {
			pair->Load(true);
		}
	}
	UpdateDiffState();
}


static bool HasModifiedUnder(const std::map<std::string, BReference<FilePair>> &pairs, const std::string &path)
{
	for (const auto &[pairPath, pair] : pairs) {
		if (IsUnder(pairPath, path) && (pair->IsModified(0) || pair->IsModified(1))) {
			return true;
		}
	}
	return false;
}


static void ShowErrors(const std::vector<std::string> &errors)
{
	if (errors.empty()) {
		return;
	}
	std::string message;
	for (size_t i = 0; i < errors.size() && i < 10; i++) {
		message += errors[i] + "\n";
	}
	if (errors.size() > 10) {
		message += "(" + std::to_string(errors.size() - 10) + " more)";
	}
	ShowError(message.c_str());
}


void CompareWindow::CopyEntry(int toPane)
{
	const std::optional<std::string> path = fTree != nullptr ? fTree->SelectedPath() : std::nullopt;
	const Node *node = path.has_value() ? fComparison->Find(*path) : nullptr;
	const int fromPane = 1 - toPane;
	if (node == nullptr || node->sides[fromPane].type == EntryType::None) {
		beep();
		return;
	}
	std::string question = "Copy “" + *path + "” to the " + kSideNames[toPane] + " side?";
	if (node->sides[toPane].type != EntryType::None) {
		question += " It replaces the " + std::string(kSideNames[toPane]) + " version";
		question += node->sides[toPane].type == EntryType::Directory
			&& node->sides[fromPane].type == EntryType::Directory
			? " (other entries in the directory are kept)." : ".";
	}
	if (HasModifiedUnder(fPairs, *path)) {
		question += " Unsaved changes are discarded.";
	}
	BAlert *alert = new BAlert("Copy", question.c_str(), "Cancel", "Copy", nullptr, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	DiffKit::Ask(this, alert, [this, fromPane, toPane, entry = *path](int32 button) {
		if (button == 1) {
			CopyEntryNow(fromPane, toPane, entry);
		}
	});
}


void CompareWindow::CopyEntryNow(int fromPane, int toPane, const std::string &entry)
{
	// The comparison may have been rescanned meanwhile.
	const Node *node = fComparison != nullptr ? fComparison->Find(entry) : nullptr;
	if (node == nullptr) {
		return;
	}
	const std::optional<std::string> path = entry;
	std::vector<std::string> errors;
	DirCompare::CopyEntry(FullPath(fromPane, *path), FullPath(toPane, *path), errors);
	ShowErrors(errors);
	ReloadPairsUnder(*path);
	// Directories created for the copy are rechecked too.
	while (node->parent != nullptr && node->parent->parent != nullptr
		&& node->parent->sides[toPane].type == EntryType::None) {
		node = node->parent;
	}
	Recheck(std::string(node->path));
}


void CompareWindow::DeleteEntry(int pane)
{
	const std::optional<std::string> path = fTree != nullptr ? fTree->SelectedPath() : std::nullopt;
	const Node *node = path.has_value() ? fComparison->Find(*path) : nullptr;
	if (node == nullptr || node->sides[pane].type == EntryType::None) {
		beep();
		return;
	}
	std::string question = "Delete “" + *path + "” from the " + kSideNames[pane] + " side? This cannot be undone.";
	if (HasModifiedUnder(fPairs, *path)) {
		question += " Unsaved changes are discarded.";
	}
	BAlert *alert = new BAlert("Delete", question.c_str(), "Cancel", "Delete", nullptr, B_WIDTH_AS_USUAL,
		B_STOP_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	DiffKit::Ask(this, alert, [this, pane, entry = *path](int32 button) {
		if (button == 1) {
			DeleteEntryNow(pane, entry);
		}
	});
}


void CompareWindow::DeleteEntryNow(int pane, const std::string &entry)
{
	const std::optional<std::string> path = entry;
	std::vector<std::string> errors;
	DirCompare::DeleteEntry(FullPath(pane, *path), errors);
	ShowErrors(errors);
	ReloadPairsUnder(*path);
	Recheck(*path);
}


void CompareWindow::ShowInTracker(int pane)
{
	std::string path = FullPath(pane, fTree != nullptr ? fTree->SelectedPath().value_or(std::string()) : "");
	// The directory itself, else the nearest existing parent.
	while (ReadEntryInfo(path).type != EntryType::Directory) {
		const size_t slash = path.rfind('/');
		if (slash == std::string::npos || slash == 0) {
			beep();
			return;
		}
		path.erase(slash);
	}
	entry_ref ref;
	if (get_ref_for_path(path.c_str(), &ref) == B_OK) {
		be_roster->Launch(&ref);
	}
}


void CompareWindow::ConfirmDiscard(const char *question, std::function<void()> then)
{
	if (!HasModifiedPairs()) {
		then();
		return;
	}
	BAlert *alert = new BAlert("Compare", question, "Cancel", "Discard", "Save", B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	DiffKit::Ask(this, alert, [this, then = std::move(then)](int32 button) {
		if (button == 2) {
			Save(true);
			if (HasModifiedPairs()) {
				return;
			}
		}
		if (button == 1 || button == 2) {
			then();
		}
	});
}


void CompareWindow::SwapSides()
{
	ConfirmDiscard("Save the modified files before swapping the sides?", [this]() {
		SwapSidesNow();
	});
}


void CompareWindow::SwapSidesNow()
{
	std::swap(fRoots[0], fRoots[1]);
	ShowPair(nullptr, "");
	fPairs.clear();
	if (fFileMode) {
		BReference<FilePair> pair(new FilePair(fRoots[0], fRoots[1]), true);
		pair->Load();
		fPairs[""] = pair;
		ShowPair(pair.Get(), "");
	} else {
		StartScan();
	}
	UpdateTitle();
}


void CompareWindow::WindowActivated(bool active)
{
	BWindow::WindowActivated(active);
	// Unmodified sides follow changes on disk; their models notify the view.
	if (active && fPair != nullptr) {
		fPair->Load();
		UpdateDiffState();
	}
}


// With modified files, the window stays open and asks; the answer closes
// it, or asks the application to quit again if the request came from there.
bool CompareWindow::QuitRequested()
{
	if (!fCloseConfirmed && HasModifiedPairs()) {
		if (!fAskingClose) {
			fAskingClose = true;
			// The application asks its windows from its own thread.
			const bool fromApplication = find_thread(nullptr) != Thread();
			BAlert *alert = new BAlert("Compare", "Save the modified files before closing?", "Cancel", "Discard",
				"Save", B_WIDTH_AS_USUAL, B_WARNING_ALERT);
			alert->SetShortcut(0, B_ESCAPE);
			DiffKit::Ask(this, alert, [this, fromApplication](int32 button) {
				fAskingClose = false;
				if (button == 2) {
					Save(true);
					if (HasModifiedPairs()) {
						return;
					}
				}
				if (button != 1 && button != 2) {
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
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void CompareWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgNewComparison:
			be_app->PostMessage(message->what);
			break;
		case kMsgQuitApplication:
			be_app->PostMessage(B_QUIT_REQUESTED);
			break;
		case kMsgScanProgress:
			TakeScanResults();
			break;
		case kMsgEntrySelected:
		case kMsgEntryInvoked:
			ShowSelected();
			break;
		case kMsgRefresh:
			if (fFileMode) {
				Reload();
			} else {
				StartScan();
			}
			break;
		case kMsgSave:
		case kMsgSaveAll:
			Save(message->what == kMsgSaveAll);
			break;
		case kMsgReload:
			Reload();
			break;
		case kMsgCopyChanges: {
			const int pane = message->GetInt32("pane", 0);
			if (fDiffView->CanCopyTo(pane)) {
				fDiffView->CopyChanges(pane);
			}
			break;
		}
		case DiffKit::kMsgChangesCopied:
		case DiffKit::kMsgDiffEdited:
			UpdateDiffState();
			break;
		case kMsgNextChange:
		case kMsgPreviousChange:
			fDiffView->GoToChunk(message->what == kMsgNextChange);
			break;
		case kMsgNextFile:
		case kMsgPreviousFile:
			if (fTree != nullptr && fTree->SelectNextDifference(message->what == kMsgNextFile)) {
				ShowSelected();
			}
			break;
		case kMsgCopyEntry:
			CopyEntry(message->GetInt32("pane", 0));
			break;
		case kMsgDeleteEntry:
			DeleteEntry(message->GetInt32("pane", 0));
			break;
		case kMsgShowInTracker:
			ShowInTracker(message->GetInt32("pane", 0));
			break;
		case kMsgSwapSides:
			SwapSides();
			break;
		case kMsgToggleShowIdentical:
			fShowIdenticalItem->SetMarked(!fShowIdenticalItem->IsMarked());
			fTree->SetShowIdentical(fShowIdenticalItem->IsMarked());
			break;
		case kMsgToggleIgnoreVcs:
			fIgnoreVcsItem->SetMarked(!fIgnoreVcsItem->IsMarked());
			fOptions.ignoreVcs = fIgnoreVcsItem->IsMarked();
			StartScan();
			break;
		case kMsgToggleSyncScrolling:
			fSyncScrollingItem->SetMarked(!fSyncScrollingItem->IsMarked());
			fDiffView->SetSyncScrolling(fSyncScrollingItem->IsMarked());
			break;
		case kMsgToggleHideUnchanged:
			fHideUnchangedItem->SetMarked(!fHideUnchangedItem->IsMarked());
			fDiffView->SetHideUnchanged(fHideUnchangedItem->IsMarked());
			break;
		case kMsgToggleWordWrap:
			fWordWrapItem->SetMarked(!fWordWrapItem->IsMarked());
			fDiffView->SetWordWrap(fWordWrapItem->IsMarked());
			break;
		case kMsgSetAlgorithm:
			fDiffView->SetAlgorithm(static_cast<DiffAlgorithm>(message->GetInt32("algorithm", 0)));
			break;
		case B_UNDO:
		case B_REDO:
		case B_CUT:
		case B_COPY:
		case B_PASTE:
		case B_SELECT_ALL: {
			BView *focus = CurrentFocus();
			if (dynamic_cast<CodeEditView*>(focus) != nullptr) {
				PostMessage(message, focus);
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // DirCompare
