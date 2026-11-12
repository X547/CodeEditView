#pragma once

#include <stddef.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <Referenceable.h>
#include <View.h>

#include <CodeEditView/TextModel.h>
#include <CodeEditView/ViewTypes.h>

#include "Chunks.h"


class BMessageRunner;
class CodeEditScrollView;
class CodeEditView;


namespace DiffKit {


class LinkMapView;
class TruncatingLabel;


// Notifications posted to the window.
// A model shown in the view was edited.
constexpr uint32 kMsgDiffEdited = 'DKed';
// Changes were copied into a pane ("pane" int32).
constexpr uint32 kMsgChangesCopied = 'DKcc';
// The label of a pane shown as a link was clicked ("pane" int32).
constexpr uint32 kMsgPaneLabelClicked = 'DKlc';

// Internal: the diff timer; an arrow of the link map was clicked ("chunk",
// "pane" int32: the pane to copy to).
constexpr uint32 kMsgDiffTimer = 'DKdt';
constexpr uint32 kMsgCopyChunk = 'DKcp';


// Two editors side by side with a link map between them, showing the diff of
// two text models. Side A of the chunks is the left pane (0), side B the
// right pane (1). The host sets the labels, which panes can be edited, and
// in which directions the link map's arrows copy chunks.
class DiffView: public BView {
private:
	struct SavedPosition {
		CodeEdit::Selection selection;
		CodeEdit::LinePosition top;
	};

	// A run of unchanged lines that may be hidden, with its fold in each
	// view; the key is its first line on side A.
	struct Region {
		size_t key {};
		CodeEdit::FoldRange folds[2];
	};

	BReference<CodeEdit::TextModel> fModels[2];
	std::string fKey;
	bool fReadOnly[2] {true, true};
	bool fCopyTo[2] {};
	CodeEditView *fViews[2] {};
	CodeEditScrollView *fPanes[2] {};
	TruncatingLabel *fLabels[2] {};
	LinkMapView *fLinkMap {};

	std::vector<Chunk> fChunks;
	bool fDiffPending {};
	std::unique_ptr<BMessageRunner> fDiffRunner;
	// Top of a view scrolled by synchronization, to ignore its notification.
	std::optional<float> fExpectedTop[2];
	std::vector<Region> fRegions;
	// Whether each region was folded in each view after the last update.
	std::vector<bool> fFolded[2];
	// Regions the user expanded, by key.
	std::set<size_t> fExpanded;
	std::map<std::string, SavedPosition> fSavedPositions;

	bool fSyncScrolling = true;
	bool fHideUnchanged = true;
	bool fWordWrap = false;
	DiffAlgorithm fAlgorithm = DiffAlgorithm::Myers;

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
	void CopyChunks(const std::vector<size_t> &indexes, int toPane, int selectionPane, const LineRange *selection);

public:
	DiffView();
	~DiffView() override;

	// Shows the diff of two models, or nothing. The selection and scroll
	// position are remembered per key.
	void SetModels(CodeEdit::TextModel *left, CodeEdit::TextModel *right, const std::string &key);
	bool HasModels() const {return fModels[0].IsSet() && fModels[1].IsSet();}
	const std::string &Key() const {return fKey;}
	CodeEditView *View(int pane) const {return fViews[pane];}
	const std::vector<Chunk> &Chunks() const {return fChunks;}

	void SetLabel(int pane, const char *text);
	// Shows the label as a link: a click posts kMsgPaneLabelClicked.
	void SetLabelLink(int pane, bool link);
	// Whether the user can edit a pane; copying changes into it still works.
	void SetReadOnly(int pane, bool readOnly);
	// The arrows of the link map copy chunks into these panes.
	void SetCopyTargets(bool toLeft, bool toRight);
	bool CanCopyTo(int pane) const {return fCopyTo[pane];}

	// Recomputes the chunks now, e.g. after a model was reloaded.
	void RefreshDiff();

	void SetSyncScrolling(bool sync);
	void SetHideUnchanged(bool hide);
	void SetWordWrap(bool wrap);
	void SetAlgorithm(DiffAlgorithm algorithm);

	// Copies changes into a pane: the chunks at the caret or selection of the
	// focused view; with selected lines, only those (Chunks.h rules: selected
	// source lines replace the chunk's target lines; selected target lines
	// are reverted). One undo group; the window gets kMsgChangesCopied.
	void CopyChanges(int toPane);
	// Copies a whole chunk into a pane.
	void CopyChunk(size_t index, int toPane);
	void GoToChunk(bool next);

	void AttachedToWindow() override;
	void MessageReceived(BMessage *message) override;
};


void ShowError(const char *message);


} // DiffKit
