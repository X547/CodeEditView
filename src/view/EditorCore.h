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
#include <CodeEditView/TextModel.h>

#include "layout/DisplayMap.h"
#include "layout/LineLayout.h"


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


struct PageInfo {
	// First fully visible row and the number of fully visible rows.
	size_t topRow {};
	size_t visibleRows {};
};


struct CaretPosition {
	size_t row {};
	uint32_t column {};
};


struct RowData {
	size_t line {};
	size_t lineStart {};
	std::string text;
	bool hasNewline {};
	LineLayout layout;
};


// Rows that need repainting since the last TakeDirty(). last == SIZE_MAX
// means "to the end".
struct DirtyRows {
	size_t first = SIZE_MAX;
	size_t last = 0;
	bool scrollRangeChanged {};
	bool marksChanged {};

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
	DisplayMap fDisplayMap;
	Selection fSelection;
	std::optional<uint32_t> fGoalColumn;
	size_t fScrollAnchor {};
	uint32_t fMaxColumns {};
	DirtyRows fDirty;
	mutable std::unordered_map<size_t, LineLayout> fLayoutCache;

	// The model must be locked by the caller of the methods below.
	void Prepare();
	void ResetFromModel();
	void ApplyChanges(const ModelChanges &changes);
	void SyncLocked();
	void TrimLayoutCache() const;
	std::string LineText(size_t line) const;
	const LineLayout &LayoutOfLine(size_t line) const;
	size_t SnapToBoundary(size_t offset) const;
	size_t PreviousBoundary(size_t offset) const;
	size_t NextBoundary(size_t offset) const;
	CaretPosition PositionOfOffsetLocked(size_t offset) const;
	size_t OffsetAtLocked(size_t row, float column) const;
	size_t SmartLineStart(size_t caret) const;
	size_t PageTarget(Movement movement, size_t row, const PageInfo &page) const;
	Selection ClampSelection(const Selection &selection) const;
	void SetSelectionLocked(const Selection &selection);
	void AddSelectionDirty(const Selection &selection);
	void ReplaceRange(size_t from, size_t to, std::string_view text, EditKind kind);
	void ApplyUndoResult(const std::optional<UndoResult> &result, const ModelChanges &changes);

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
	void SetSettings(const EditorSettings &settings);

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
	void Paste(std::string_view text);
	void DeleteSelection();
	void SelectAll();
	void SetSelection(const Selection &selection);
	bool Undo();
	bool Redo();

	DirtyRows TakeDirty();
};


} // CodeEdit
