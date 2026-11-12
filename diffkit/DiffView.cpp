#include "DiffView.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <exception>

#include <Alert.h>
#include <LayoutBuilder.h>
#include <MessageRunner.h>
#include <Window.h>

#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>

#include "DiffColors.h"
#include "LinkMapView.h"
#include "TruncatingLabel.h"


namespace DiffKit {


using CodeEdit::Decoration;
using CodeEdit::DecorationKind;
using CodeEdit::FoldRange;
using CodeEdit::FoldSource;
using CodeEdit::LinePosition;
using CodeEdit::ModelLock;
using CodeEdit::Rope;
using CodeEdit::Selection;
using CodeEdit::TextModel;


// Delay between an edit and recomputing the diff.
static constexpr bigtime_t kDiffDelay = 150000;
// Lines kept visible around chunks when unchanged regions are hidden.
static constexpr size_t kContextLines = 3;
// Unchanged regions are hidden only if at least this many lines would be.
static constexpr size_t kMinHiddenLines = 4;
static constexpr uint8 kLineMix = 56;


void ShowError(const char *message)
{
	BAlert *alert = new BAlert("Error", message, "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(nullptr);
}


static std::string ModelText(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


static size_t LineOffset(const Rope &text, size_t line)
{
	return line <= text.NewlineCount() ? text.LineStart(line) : text.Length();
}


static rgb_color WithAlpha(rgb_color color, uint8 alpha)
{
	color.alpha = alpha;
	return color;
}


// Lines of a view's selection; a selection ending at a line start does not
// include that line.
static LineRange SelectedLines(CodeEditView *view, bool &empty)
{
	const Selection selection = view->GetSelection();
	const size_t start = std::min(selection.anchor, selection.caret);
	const size_t end = std::max(selection.anchor, selection.caret);
	empty = start == end;
	ModelLock lock(*view->Model());
	const Rope &text = view->Model()->Text();
	LineRange range {text.LineOfOffset(start), text.LineOfOffset(end)};
	if (end > start && range.last > range.first && end == text.LineStart(range.last)) {
		range.last--;
	}
	return range;
}


DiffView::DiffView():
	BView("diffView", 0)
{
	for (int pane = 0; pane < 2; pane++) {
		fViews[pane] = new CodeEditView(pane == 0 ? "leftView" : "rightView");
		fPanes[pane] = new CodeEditScrollView(pane == 0 ? "leftPane" : "rightPane", fViews[pane]);
		fLabels[pane] = new TruncatingLabel(pane == 0 ? "leftLabel" : "rightLabel", "");
		BMessage *clicked = new BMessage(kMsgPaneLabelClicked);
		clicked->AddInt32("pane", pane);
		fLabels[pane]->SetMessage(clicked);
	}
	fLinkMap = new LinkMapView(this);
	ApplySettings();

	// The pane frames reach the edges of this view; only the labels have
	// space around them.
	BLayoutBuilder::Grid<>(this, 0, 0)
		.AddGroup(B_HORIZONTAL, 0, 0, 0)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(fLabels[0])
		.End()
		.AddGroup(B_HORIZONTAL, 0, 2, 0)
			.SetInsets(B_USE_SMALL_SPACING)
			.Add(fLabels[1])
		.End()
		.Add(fPanes[0], 0, 1)
		.Add(fLinkMap, 1, 1)
		.Add(fPanes[1], 2, 1)
		.SetRowWeight(1, 1)
		.SetColumnWeight(0, 1)
		.SetColumnWeight(2, 1);
}


DiffView::~DiffView()
{
}


void DiffView::AttachedToWindow()
{
	BView::AttachedToWindow();
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	for (CodeEditView *view : fViews) {
		view->SetTarget(BMessenger(this));
	}
	for (TruncatingLabel *label : fLabels) {
		label->SetTarget(Window());
	}
}


void DiffView::ApplySettings()
{
	for (int pane = 0; pane < 2; pane++) {
		CodeEdit::EditorSettings settings = fViews[pane]->Settings();
		settings.readOnly = fReadOnly[pane] || !HasModels();
		settings.wordWrap = fWordWrap;
		if (!(settings == fViews[pane]->Settings())) {
			fViews[pane]->SetSettings(settings);
		}
	}
}


void DiffView::SavePosition()
{
	if (!HasModels()) {
		return;
	}
	CodeEditView *view = fViews[1];
	fSavedPositions[fKey] = {view->GetSelection(), view->LineAtY(view->Bounds().top)};
}


void DiffView::RestorePosition()
{
	CodeEditView *view = fViews[1];
	const auto it = fSavedPositions.find(fKey);
	if (it != fSavedPositions.end()) {
		view->SetSelection(it->second.selection);
		view->ScrollLineTo(it->second.top, 0, 0);
	} else if (!fChunks.empty()) {
		// Start at the first change.
		size_t lineStart;
		{
			ModelLock lock(*view->Model());
			lineStart = LineOffset(view->Model()->Text(), fChunks.front().bFirst);
		}
		view->SetSelection({lineStart, lineStart});
		view->RevealLine(fChunks.front().bFirst);
	}
	SyncScroll(1);
}


void DiffView::SetModels(TextModel *left, TextModel *right, const std::string &key)
{
	if (left == fModels[0].Get() && right == fModels[1].Get() && key == fKey) {
		return;
	}
	SavePosition();
	if (left == nullptr || right == nullptr) {
		left = right = nullptr;
	}
	fModels[0].SetTo(left);
	fModels[1].SetTo(right);
	fKey = key;
	fExpanded.clear();
	fRegions.clear();
	fFolded[0].clear();
	fFolded[1].clear();
	fExpectedTop[0].reset();
	fExpectedTop[1].reset();
	fChunks.clear();

	for (int pane = 0; pane < 2; pane++) {
		fViews[pane]->SetModel(fModels[pane].Get());
	}
	ApplySettings();
	RefreshDiff();
	if (HasModels()) {
		RestorePosition();
	}
}


void DiffView::SetLabel(int pane, const char *text)
{
	fLabels[pane]->SetLabel(text);
}


void DiffView::SetLabelLink(int pane, bool link)
{
	fLabels[pane]->SetLink(link);
}


void DiffView::SetReadOnly(int pane, bool readOnly)
{
	fReadOnly[pane] = readOnly;
	ApplySettings();
}


void DiffView::SetCopyTargets(bool toLeft, bool toRight)
{
	if (fCopyTo[0] == toLeft && fCopyTo[1] == toRight) {
		return;
	}
	fCopyTo[0] = toLeft;
	fCopyTo[1] = toRight;
	fLinkMap->Invalidate();
}


void DiffView::ScheduleDiff()
{
	fDiffPending = true;
	BMessage message(kMsgDiffTimer);
	fDiffRunner = std::make_unique<BMessageRunner>(BMessenger(this), &message, kDiffDelay, 1);
}


void DiffView::RefreshDiff()
{
	fDiffRunner.reset();
	fDiffPending = false;
	fChunks.clear();
	if (HasModels()) {
		try {
			fChunks = ComputeChunks(ModelText(*fModels[0]), ModelText(*fModels[1]), fAlgorithm);
		} catch (const std::exception &e) {
			fprintf(stderr, "Diff: %s\n", e.what());
		}
	}
	UpdateDecorations();
	ApplyHostFolds();
	fLinkMap->Invalidate();
}


void DiffView::UpdateDecorations()
{
	std::vector<Decoration> decorations[2];
	for (int pane = 0; pane < 2 && HasModels(); pane++) {
		const bool onA = pane == 0;
		TextModel &model = *fModels[pane];
		ModelLock lock(model);
		const Rope &text = model.Text();
		for (const Chunk &chunk : fChunks) {
			const size_t first = onA ? chunk.aFirst : chunk.bFirst;
			const size_t count = onA ? chunk.aCount : chunk.bCount;
			rgb_color color = kChangeColor;
			if (chunk.aCount == 0) {
				color = kInsertColor;
			} else if (chunk.bCount == 0) {
				color = kDeleteColor;
			}
			const size_t start = LineOffset(text, first);
			if (count == 0) {
				decorations[pane].push_back({DecorationKind::GapLine, start, start, color});
			} else {
				decorations[pane].push_back({DecorationKind::LineBackground, start,
					LineOffset(text, first + count), WithAlpha(color, kLineMix)});
			}
		}
	}
	fViews[0]->SetDecorations(std::move(decorations[0]));
	fViews[1]->SetDecorations(std::move(decorations[1]));
}


// Runs of unchanged lines long enough to hide, leaving context lines next to
// the chunks visible. A run has the same length in both views. The selection
// of either view stays visible with context lines around it, e.g. after its
// lines were copied and became unchanged; the run is split around it.
std::vector<DiffView::Region> DiffView::UnchangedRegions()
{
	std::vector<Region> regions;
	if (!fHideUnchanged || !HasModels()) {
		return regions;
	}
	const size_t lineCounts[2] = {fViews[0]->LineCount(), fViews[1]->LineCount()};
	LineRange selections[2];
	for (int pane = 0; pane < 2; pane++) {
		bool empty;
		selections[pane] = SelectedLines(fViews[pane], empty);
	}
	size_t a = 0;
	size_t b = 0;
	for (size_t i = 0; i <= fChunks.size(); i++) {
		const bool atEnd = i == fChunks.size();
		const size_t count = atEnd
			? std::min(lineCounts[0] - std::min(a, lineCounts[0]), lineCounts[1] - std::min(b, lineCounts[1]))
			: fChunks[i].aFirst - a;
		const bool atStart = a == 0 && b == 0;
		// Offsets in the run of the header line and of the last hidden line.
		const size_t header = atStart ? 0 : kContextLines - 1;
		const size_t keepAfter = atEnd ? 0 : kContextLines;
		if (count > header + keepAfter + kMinHiddenLines) {
			const size_t hiddenFirst = header + 1;
			const size_t hiddenLast = count - keepAfter - 1;
			// Offsets kept visible for selections that would be hidden.
			std::vector<LineRange> keep;
			const size_t runStarts[2] = {a, b};
			for (int pane = 0; pane < 2; pane++) {
				const LineRange &selection = selections[pane];
				const size_t start = runStarts[pane];
				if (selection.last < start + hiddenFirst || selection.first > start + hiddenLast) {
					continue;
				}
				const size_t first = std::max(selection.first, start) - start;
				const size_t last = std::min(selection.last, start + count - 1) - start;
				keep.push_back({first > kContextLines ? first - kContextLines : 0, last + kContextLines});
			}
			std::sort(keep.begin(), keep.end(), [](const LineRange &x, const LineRange &y) {
				return x.first < y.first;
			});

			auto addRegion = [&](size_t first, size_t last) {
				if (last + 1 < first + kMinHiddenLines) {
					return;
				}
				Region region;
				region.key = a + first;
				region.folds[0] = {a + first - 1, a + last, FoldSource::Host};
				region.folds[1] = {b + first - 1, b + last, FoldSource::Host};
				regions.push_back(region);
			};
			size_t next = hiddenFirst;
			for (const LineRange &range : keep) {
				if (range.first > next) {
					addRegion(next, std::min(range.first - 1, hiddenLast));
				}
				next = std::max(next, range.last + 1);
			}
			if (next <= hiddenLast) {
				addRegion(next, hiddenLast);
			}
		}
		if (!atEnd) {
			a = fChunks[i].aFirst + fChunks[i].aCount;
			b = fChunks[i].bFirst + fChunks[i].bCount;
		}
	}
	return regions;
}


// Gives both views the regions as host folds, folded unless expanded.
void DiffView::ApplyHostFolds(bool foldAtCaret)
{
	fRegions = UnchangedRegions();
	for (int pane = 0; pane < 2; pane++) {
		std::vector<CodeEdit::HostFold> folds;
		for (const Region &region : fRegions) {
			const FoldRange &fold = region.folds[pane];
			folds.push_back({fold.headerLine, fold.lastLine, fExpanded.count(region.key) == 0});
		}
		fViews[pane]->SetHostFolds(folds, foldAtCaret);
		fFolded[pane] = FoldedRegions(pane);
	}
}


std::vector<bool> DiffView::FoldedRegions(int pane)
{
	const std::vector<FoldRange> folds = fViews[pane]->Folds();
	std::vector<bool> folded;
	for (const Region &region : fRegions) {
		folded.push_back(std::find(folds.begin(), folds.end(), region.folds[pane]) != folds.end());
	}
	return folded;
}


// A region the user folded or unfolded in one view (also by moving the
// caret into it) follows in the other.
void DiffView::CheckFoldChanges()
{
	// Edits move the folds until the diff is computed again.
	if (fDiffPending) {
		return;
	}
	bool changed = false;
	for (int pane = 0; pane < 2; pane++) {
		const std::vector<bool> folded = FoldedRegions(pane);
		for (size_t i = 0; i < fRegions.size() && i < fFolded[pane].size(); i++) {
			if (folded[i] == fFolded[pane][i]) {
				continue;
			}
			if (folded[i]) {
				fExpanded.erase(fRegions[i].key);
			} else {
				fExpanded.insert(fRegions[i].key);
			}
			changed = true;
		}
	}
	if (changed) {
		ApplyHostFolds();
	}
}

// Scrolls the other view so that the line at the middle of this one is at
// its middle too.
void DiffView::SyncScroll(int from)
{
	if (!fSyncScrolling || !HasModels()) {
		return;
	}
	CodeEditView *source = fViews[from];
	const int to = 1 - from;
	CodeEditView *target = fViews[to];
	const BRect bounds = source->Bounds();
	const float middle = floorf(bounds.Height() / 2);
	const LinePosition position = source->LineAtY(bounds.top + middle);
	const double mapped = MapLine(fChunks, position.line + static_cast<double>(position.fraction), from == 0);
	const double line = std::max(0.0, floor(mapped));
	const LinePosition targetPosition {static_cast<size_t>(line), static_cast<float>(mapped - line)};

	const BPoint before = target->Bounds().LeftTop();
	target->ScrollLineTo(targetPosition, floorf(target->Bounds().Height() / 2), bounds.left);
	if (target->Bounds().LeftTop() != before) {
		fExpectedTop[to] = target->Bounds().top;
	}
}


void DiffView::SetSyncScrolling(bool sync)
{
	fSyncScrolling = sync;
	SyncScroll(1);
}


void DiffView::SetHideUnchanged(bool hide)
{
	fHideUnchanged = hide;
	fExpanded.clear();
	ApplyHostFolds(true);
	SyncScroll(1);
}


void DiffView::SetWordWrap(bool wrap)
{
	fWordWrap = wrap;
	ApplySettings();
}


void DiffView::SetAlgorithm(DiffAlgorithm algorithm)
{
	fAlgorithm = algorithm;
	RefreshDiff();
}


CodeEditView *DiffView::FocusedView() const
{
	return fViews[0]->IsFocus() ? fViews[0] : fViews[1];
}


void DiffView::CopyChunks(const std::vector<size_t> &indexes, int toPane, int selectionPane,
	const LineRange *selection)
{
	if (indexes.empty()) {
		return;
	}
	TextModel &left = *fModels[0];
	TextModel &right = *fModels[1];
	// A selection in the source pane picks the lines to copy; one in the
	// target pane picks the lines to revert. The chunks are swapped when the
	// left pane is the one the functions call B.
	const bool selectionInRight = selection == nullptr || selectionPane == 1;
	try {
		if (toPane == 0) {
			if (selectionInRight) {
				ApplyToA(left, right, fChunks, indexes, selection);
			} else {
				RevertB(right, left, SwapSides(fChunks), indexes, selection);
			}
		} else {
			if (selectionInRight) {
				RevertB(left, right, fChunks, indexes, selection);
			} else {
				ApplyToA(right, left, SwapSides(fChunks), indexes, selection);
			}
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
		return;
	}
	RefreshDiff();
	BMessage message(kMsgChangesCopied);
	message.AddInt32("pane", toPane);
	Window()->PostMessage(&message);
}


void DiffView::CopyChanges(int toPane)
{
	if (!HasModels()) {
		return;
	}
	if (fDiffPending) {
		RefreshDiff();
	}
	CodeEditView *view = FocusedView();
	const int pane = view == fViews[0] ? 0 : 1;
	bool empty;
	const LineRange range = SelectedLines(view, empty);
	const std::vector<size_t> indexes = TouchedChunks(fChunks, range, pane == 0);
	CopyChunks(indexes, toPane, pane, empty ? nullptr : &range);
}


void DiffView::CopyChunk(size_t index, int toPane)
{
	if (!HasModels() || !fCopyTo[toPane] || fDiffPending || index >= fChunks.size()) {
		return;
	}
	CopyChunks({index}, toPane, 1, nullptr);
}


void DiffView::GoToChunk(bool next)
{
	if (!HasModels()) {
		return;
	}
	if (fDiffPending) {
		RefreshDiff();
	}
	CodeEditView *view = FocusedView();
	const bool onA = view == fViews[0];
	const size_t caretLine = view->CaretLocation().line;
	std::optional<size_t> target;
	for (size_t i = 0; i < fChunks.size(); i++) {
		const size_t first = onA ? fChunks[i].aFirst : fChunks[i].bFirst;
		if (next && first > caretLine) {
			target = first;
			break;
		}
		if (!next && first < caretLine) {
			target = first;
		}
	}
	if (!target.has_value()) {
		return;
	}
	size_t offset;
	{
		ModelLock lock(*view->Model());
		offset = LineOffset(view->Model()->Text(), *target);
	}
	view->RevealLine(*target);
	view->SetSelection({offset, offset});
	view->MakeFocus(true);
	SyncScroll(onA ? 0 : 1);
}


void DiffView::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case CodeEdit::kMsgContentsChanged:
			ScheduleDiff();
			Window()->PostMessage(kMsgDiffEdited);
			break;
		case CodeEdit::kMsgViewportChanged: {
			void *pointer = nullptr;
			message->FindPointer("view", &pointer);
			const int pane = pointer == fViews[0] ? 0 : (pointer == fViews[1] ? 1 : -1);
			if (pane < 0) {
				break;
			}
			fLinkMap->Invalidate();
			CheckFoldChanges();
			if (!message->GetBool("scrolled", false)) {
				break;
			}
			const float top = fViews[pane]->Bounds().top;
			if (fExpectedTop[pane].has_value() && *fExpectedTop[pane] == top) {
				fExpectedTop[pane].reset();
				break;
			}
			fExpectedTop[pane].reset();
			SyncScroll(pane);
			break;
		}
		case kMsgDiffTimer:
			RefreshDiff();
			break;
		case kMsgCopyChunk:
			CopyChunk(static_cast<size_t>(message->GetInt32("chunk", -1)), message->GetInt32("pane", 0));
			break;
		case CodeEdit::kMsgCaretMoved:
			break;
		default:
			BView::MessageReceived(message);
			break;
	}
}


} // DiffKit
