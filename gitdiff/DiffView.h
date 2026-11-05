#pragma once

#include <stddef.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <Referenceable.h>
#include <View.h>

#include <CodeEditView/TextModel.h>
#include <CodeEditView/ViewTypes.h>

#include "Chunks.h"
#include "FileDocument.h"


class BMessageRunner;
class BStringView;
class CodeEditScrollView;
class CodeEditView;


namespace GitDiff {


class LinkMapView;
class Repository;


enum class DiffKind {
	// Index (left) against the working tree (right).
	Unstaged,
	// HEAD (left) against the index (right).
	Staged,
};


// Two editors side by side with a link map between them, showing one diff
// of a FileDocument. The left side is read-only. Side A of the chunks is the
// left side.
class DiffView: public BView {
private:
	struct SavedPosition {
		CodeEdit::Selection selection;
		CodeEdit::LinePosition top;
	};

	Repository *fRepository;
	BReference<FileDocument> fDocument;
	DiffKind fKind = DiffKind::Unstaged;
	CodeEditView *fViews[2] {};
	CodeEditScrollView *fPanes[2] {};
	BStringView *fLabels[2] {};
	LinkMapView *fLinkMap {};

	std::vector<Chunk> fChunks;
	bool fDiffPending {};
	std::unique_ptr<BMessageRunner> fDiffRunner;
	// Top of a view scrolled by synchronization, to ignore its notification.
	std::optional<float> fExpectedTop[2];
	// A run of unchanged lines that may be hidden, with its fold in each
	// view; the key is its first line on side A.
	struct Region {
		size_t key {};
		CodeEdit::FoldRange folds[2];
	};

	std::vector<Region> fRegions;
	// Whether each region was folded in each view after the last update.
	std::vector<bool> fFolded[2];
	// Regions the user expanded, by key.
	std::set<size_t> fExpanded;
	std::map<std::pair<std::string, DiffKind>, SavedPosition> fSavedPositions;

	bool fSyncScrolling = true;
	bool fHideUnchanged = true;
	bool fWordWrap = false;
	DiffAlgorithm fAlgorithm = DiffAlgorithm::Myers;

	Side SideOf(int pane) const;
	void ScheduleDiff();
	void UpdateDecorations();
	std::vector<Region> UnchangedRegions();
	void ApplyHostFolds(bool foldAtCaret = false);
	std::vector<bool> FoldedRegions(int pane);
	void CheckFoldChanges();
	void SyncScroll(int from);
	void ApplySettings();
	void SavePosition();
	void RestorePosition();
	CodeEditView *FocusedView() const;
	void TransferChunks(const std::vector<size_t> &indexes, const LineRange *selection);
	void WriteIndex();

public:
	explicit DiffView(Repository *repository);
	~DiffView() override;

	// Shows a diff of the document, or nothing.
	void SetDiff(FileDocument *document, DiffKind kind);
	FileDocument *Document() const {return fDocument.Get();}
	DiffKind Kind() const {return fKind;}
	CodeEditView *View(int pane) const {return fViews[pane];}
	CodeEditView *RightView() const {return fViews[1];}
	const std::vector<Chunk> &Chunks() const {return fChunks;}

	// Recomputes the chunks now, e.g. after the document was reloaded.
	void RefreshDiff();
	// Updates the labels and the read-only state of the right view.
	void UpdateState();

	void SetSyncScrolling(bool sync);
	void SetHideUnchanged(bool hide);
	void SetWordWrap(bool wrap);
	void SetAlgorithm(DiffAlgorithm algorithm);

	// Stage (unstaged diff) or unstage (staged diff): selected lines in the
	// right view, else the chunks at the caret or selection of the focused
	// view. The index is written.
	void Stage();
	// Unstaged diff: reverts the working tree buffer like Stage() picks.
	void Revert();
	void TransferChunk(size_t index);
	void GoToChunk(bool next);

	void AttachedToWindow() override;
	void MessageReceived(BMessage *message) override;
};


void ShowError(const char *message);


} // GitDiff
