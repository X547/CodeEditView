#pragma once

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Messenger.h>
#include <Referenceable.h>

#include <CodeEditView/EditorSettings.h>
#include <CodeEditView/Outline.h>
#include <CodeEditView/TextModel.h>
#include <CodeEditView/Theme.h>

#include "layout/DisplayMap.h"
#include "layout/LineLayout.h"
#include "syntax/Highlighter.h"


namespace CodeEdit {


enum class Movement {
	CharacterLeft,
	CharacterRight,
	WordLeft,
	WordRight,
	SubwordLeft,
	SubwordRight,
	LineStart,
	LineEnd,
	PreviousLine,
	NextLine,
	PageTop,
	PageBottom,
	FileStart,
	FileEnd,
};


enum class Deletion {
	CharacterLeft,
	CharacterRight,
	ToLineStart,
	ToLineEnd,
};


enum class SelectionUnit {
	Character,
	Word,
	Line,
};


struct TextRange {
	size_t start {};
	size_t end {};

	bool operator==(const TextRange &other) const = default;
};


// Cell under a point: the cluster start, or the line end past the last cluster.
struct CellHit {
	size_t offset {};
	bool pastLineEnd {};
};


// A fold hides the lines after its header line up to lastLine.
struct FoldRange {
	size_t headerLine {};
	size_t lastLine {};

	bool operator==(const FoldRange &other) const = default;
};


struct FoldMarker {
	size_t line {};
	bool folded {};

	bool operator==(const FoldMarker &other) const = default;
};


// A bracket next to the caret and its partner.
struct BracketPair {
	size_t bracket {};
	size_t partner {};

	bool operator==(const BracketPair &other) const = default;
};


struct PageInfo {
	// First fully visible row and the number of fully visible rows.
	size_t topRow {};
	size_t visibleRows {};
};


struct CaretPosition {
	size_t row {};
	uint32_t column {};
};


// A display row: the clusters of its line from rowStart to rowEnd, drawn
// starting at cell indent.
struct RowData {
	size_t line {};
	size_t lineStart {};
	// The whole line.
	std::string text;
	bool hasNewline {};
	LineLayout layout;
	// Per byte of text; empty without syntax highlighting.
	std::vector<HighlightStyle> styles;
	uint32_t subRow {};
	bool lastRow = true;
	// Byte offsets in text.
	size_t rowStart {};
	size_t rowEnd {};
	// Line column of the cluster at rowStart.
	uint32_t rowColumn {};
	uint32_t indent {};

	// Row column of an offset in text within the row.
	uint32_t DisplayColumn(size_t offset) const
	{
		return layout.ColumnOfOffset(offset) - rowColumn + indent;
	}
};


// Location of a display row within its line.
struct RowSpan {
	size_t line {};
	size_t lineStart {};
	uint32_t subRow {};
	WrapRow row;
	// Offset in the line where the next row starts, or the line length.
	size_t end {};
	bool last {};
};


// Rows that need repainting since the last TakeDirty(). last == SIZE_MAX
// means "to the end".
struct DirtyRows {
	size_t first = SIZE_MAX;
	size_t last = 0;
	bool scrollRangeChanged {};
	bool marksChanged {};
	// The text or its syntax tree changed.
	bool contentsChanged {};

	bool IsEmpty() const {return first > last;}
	void Add(size_t from, size_t to);
	void AddAll() {Add(0, SIZE_MAX);}
};


// Editing state and commands of one view, independent of drawing. Runs in the
// view's thread; every public method locks the model and first applies
// pending model changes, so its state always matches the current text.
class EditorCore {
private:
	BReference<TextModel> fModel;
	ObserverId fObserver = kNoObserver;
	EditorSettings fSettings;
	LayoutOptions fLayoutOptions;
	// Lines left stale by a wrap width change keep their old row count until
	// re-wrapped. Queries never re-wrap: that shifts the rows after a line,
	// which the view must compensate for (see WrapRows()).
	DisplayMap fDisplayMap;
	DirtyRows fDirty;
	Selection fSelection;
	std::optional<uint32_t> fGoalColumn;
	size_t fScrollAnchor {};
	uint32_t fMaxColumns {};
	// Wrap width in cells given by the view; 0 until known.
	uint32_t fWrapColumns {};
	mutable std::unordered_map<size_t, LineLayout> fLayoutCache;
	mutable std::unordered_map<size_t, std::vector<WrapRow>> fWrapCache;
	QueryCursor fQueryCursor;
	std::vector<Selection> fExpandHistory;
	std::optional<Selection> fExpandedSelection;
	std::optional<BracketPair> fBracketMatch;
	// Sorted by header line; may nest.
	std::vector<FoldRange> fFolds;

	// The model must be locked by the caller of the methods below.
	void Prepare();
	void ResetFromModel();
	void Relayout();
	void ApplyChanges(const ModelChanges &changes);
	void SyncLocked();
	void TrimLayoutCache() const;
	std::string LineText(size_t line) const;
	const LineLayout &LayoutOfLine(size_t line) const;
	bool IsWrappingLocked() const {return fSettings.wordWrap && fWrapColumns > 0;}
	std::vector<WrapRow> ComputeWrap(const LineLayout &layout) const;
	const std::vector<WrapRow> &WrapOfLine(size_t line) const;
	bool UpdateWrapRows(size_t line);
	bool RewrapLine(size_t line);
	void EnsureWrapped(size_t line);
	bool WrapRowsLocked(size_t firstRow, size_t count);
	DisplayRow RowAtLocked(size_t row) const;
	RowSpan SpanOf(size_t line, uint32_t subRow) const;
	RowSpan SpanOfOffset(size_t offset) const;
	size_t SnapToBoundary(size_t offset) const;
	size_t PreviousBoundary(size_t offset) const;
	size_t NextBoundary(size_t offset) const;
	CaretPosition PositionOfOffsetLocked(size_t offset) const;
	uint32_t LineColumnOfOffset(size_t offset) const;
	size_t OffsetAtLocked(size_t row, float column) const;
	size_t SmartLineStart(size_t caret) const;
	size_t PageTarget(Movement movement, size_t row, const PageInfo &page) const;
	Selection ClampSelection(const Selection &selection) const;
	void SetSelectionLocked(const Selection &selection);
	void AddSelectionDirty(const Selection &selection);
	void ReplaceRange(size_t from, size_t to, std::string_view text, EditKind kind);
	void ApplyUndoResult(const std::optional<UndoResult> &result, const ModelChanges &changes);
	std::optional<TextRange> BalancedRangeAtLocked(size_t offset) const;

	void ApplyFolds();
	bool AdjustFolds(const EditRecord &edit);
	std::optional<FoldRange> HidingFold(size_t line) const;
	bool UnfoldLines(size_t first, size_t last);
	void AddFold(const FoldRange &fold);
	size_t FirstNonBlank(size_t line) const;
	uint32_t IndentSize() const;
	std::vector<FoldRange> FoldCandidatesLocked(size_t firstLine, size_t lastLine, bool headersInRange);
	bool IsFoldedLocked(size_t headerLine) const;

public:
	explicit EditorCore(const BReference<TextModel> &model);
	~EditorCore();

	EditorCore(const EditorCore &other) = delete;
	EditorCore &operator=(const EditorCore &other) = delete;

	TextModel *Model() const {return fModel.Get();}
	ObserverId Observer() const {return fObserver;}

	// Notifications are sent to messenger while attached.
	void Attach(const BMessenger &messenger);
	void Detach();
	void Sync();

	const EditorSettings &Settings() const {return fSettings;}
	const LayoutOptions &GetLayoutOptions() const {return fLayoutOptions;}
	void SetSettings(const EditorSettings &settings);

	// Word wrap. The width in cells comes from the view. After a change,
	// lines are stale until re-wrapped by WrapRows() (the rows about to be
	// shown), WrapFromScrollAnchor() (count rows from the scroll anchor) or
	// RewrapStep() (up to maxLines lines in the background; returns whether
	// stale lines remain). WrapRows() returns whether row counts changed.
	void SetWrapColumns(uint32_t columns);
	bool IsWrapping();
	bool WrapRows(size_t firstRow, size_t count);
	void WrapFromScrollAnchor(size_t count);
	bool RewrapStep(size_t maxLines);

	// Queries.
	Selection GetSelection();
	size_t RowCount();
	size_t LineCount();
	uint32_t MaxColumns();
	std::vector<RowData> Rows(size_t first, size_t count);
	std::vector<DisplayRow> DisplayRows(size_t first, size_t count);
	CaretPosition PositionOfOffset(size_t offset);
	size_t OffsetAt(size_t row, float column);
	size_t RowStartOffset(size_t row);
	CellHit HitTestCell(size_t row, float column);
	// Model line and display column of the caret.
	TextPoint CaretLocation();
	std::string SelectedText();

	// Mouse selection. For words, offset is the start of the clicked cluster:
	// a word character selects its word, blanks select the blank run, other
	// characters select themselves.
	TextRange UnitRangeAt(size_t offset, SelectionUnit unit);
	// Contents between a bracket at offset and its partner.
	std::optional<TextRange> BalancedRangeAt(size_t offset);
	// Selects from the anchor range to the unit containing offset.
	void ExtendSelection(const TextRange &anchor, size_t offset, SelectionUnit unit);

	// Drag and drop; both select the inserted text.
	void MoveText(const TextRange &source, size_t target);
	void InsertTextAt(size_t offset, std::string_view text);

	// Offset that follows edits like text before it; used to keep the scroll
	// position stable when other views edit above it.
	void SetScrollAnchor(size_t offset) {fScrollAnchor = offset;}
	size_t ScrollAnchor() const {return fScrollAnchor;}

	// Commands.
	void Move(Movement movement, bool extend, const PageInfo &page);
	void Delete(Deletion deletion);
	void InsertText(std::string_view text);
	void InsertNewline();
	void InsertTab();
	// Adds or removes one indentation level at the start of each selected
	// line (the caret line without a selection). Empty lines are skipped.
	void ChangeIndent(bool increase);
	void Paste(std::string_view text);
	void DeleteSelection();
	void SelectAll();
	void SetSelection(const Selection &selection);
	bool Undo();
	bool Redo();

	// Recomputes the bracket pair highlighted at the caret (the character
	// after the caret first, then the one before) and marks changed rows dirty.
	void UpdateBracketMatch();
	std::optional<BracketPair> BracketMatch() const {return fBracketMatch;}

	// Folding. Candidates come from the syntax tree; folds of any line range
	// can also be added manually.
	std::vector<FoldRange> FoldCandidates(size_t firstLine, size_t lastLine);
	std::vector<FoldMarker> FoldMarkers(size_t firstLine, size_t lastLine);
	std::vector<FoldRange> Folds();
	bool IsFolded(size_t headerLine);
	void Fold(const FoldRange &fold);
	void Unfold(size_t headerLine);
	void ToggleFoldAtLine(size_t line);
	// Folds a multi-line selection, otherwise toggles the fold at the caret
	// line or folds the innermost candidate containing it.
	void ToggleFold();
	void FoldAll();
	void UnfoldAll();

	// Syntax navigation (no-ops without a syntax tree).
	void ExpandSelection();
	void ShrinkSelection();
	void MoveToDefinition(bool forward);
	// Definitions in the text; empty without a syntax tree.
	std::vector<OutlineItem> Outline();

	// Marks of lines firstLine to lastLine, sorted by line.
	std::vector<MarkInfo> Marks(size_t firstLine, size_t lastLine);
	// Unfolds folds hiding the line; returns the line's first row.
	size_t RevealLine(size_t line);

	DirtyRows TakeDirty();
};


} // CodeEdit
