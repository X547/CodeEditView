#include "EditorCore.h"

#include <algorithm>

#include "WordMotion.h"


namespace CodeEdit {


static constexpr size_t kMaxCachedLayouts = 2048;


void DirtyRows::Add(size_t from, size_t to)
{
	first = std::min(first, from);
	last = std::max(last, to);
}


static Selection MapSelection(const Selection &selection, const EditRecord &edit)
{
	const bool forward = selection.anchor <= selection.caret;
	size_t start = std::min(selection.anchor, selection.caret);
	size_t end = std::max(selection.anchor, selection.caret);
	if (start == end) {
		start = end = edit.MapOffset(start, Gravity::Right);
	} else {
		// Text inserted at the selection edges stays outside of it.
		start = edit.MapOffset(start, Gravity::Right);
		end = edit.MapOffset(end, Gravity::Left);
		if (start > end) {
			start = end;
		}
	}
	return forward ? Selection {start, end} : Selection {end, start};
}


static std::string NormalizeLineEndings(std::string_view text)
{
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\r') {
			result += '\n';
			if (i + 1 < text.size() && text[i + 1] == '\n') {
				i++;
			}
			continue;
		}
		result += text[i];
	}
	return result;
}


//#pragma mark - EditorCore

EditorCore::EditorCore(const BReference<TextModel> &model):
	fModel(model)
{
	fLayoutOptions.tabWidth = fSettings.tabWidth;
	fLayoutOptions.ambiguousWidth = fSettings.ambiguousWidth;

	ModelLock lock(*fModel);
	fObserver = fModel->AddObserver();
	ResetFromModel();
}


EditorCore::~EditorCore()
{
	ModelLock lock(*fModel);
	fModel->RemoveObserver(fObserver);
}


void EditorCore::Attach(const BMessenger &messenger)
{
	ModelLock lock(*fModel);
	fModel->SetObserverMessenger(fObserver, messenger);
	SyncLocked();
}


void EditorCore::Detach()
{
	ModelLock lock(*fModel);
	fModel->ClearObserverMessenger(fObserver);
}


void EditorCore::Sync()
{
	ModelLock lock(*fModel);
	SyncLocked();
}


void EditorCore::SyncLocked()
{
	ApplyChanges(fModel->Sync(fObserver));
}


void EditorCore::SetSettings(const EditorSettings &settings)
{
	fSettings = settings;
	fLayoutOptions.tabWidth = std::max<uint32_t>(1, settings.tabWidth);
	fLayoutOptions.ambiguousWidth = std::clamp<uint32_t>(settings.ambiguousWidth, 1, 2);

	ModelLock lock(*fModel);
	SyncLocked();
	ResetFromModel();
}


void EditorCore::ResetFromModel()
{
	const Rope &text = fModel->Text();
	fLayoutCache.clear();
	fDisplayMap.Reset(text.LineCount());

	// TODO: computing the widest line lays out the whole text; do it lazily
	// for large files.
	fMaxColumns = 0;
	for (size_t line = 0; line < text.LineCount(); line++) {
		fMaxColumns = std::max(fMaxColumns, LineLayout(LineText(line), fLayoutOptions).Width());
	}

	fSelection = ClampSelection(fSelection);
	fScrollAnchor = std::min(fScrollAnchor, text.Length());
	fGoalColumn.reset();
	fDirty.AddAll();
	fDirty.scrollRangeChanged = true;
	fDirty.marksChanged = true;
}


void EditorCore::ApplyChanges(const ModelChanges &changes)
{
	if (changes.reset) {
		ResetFromModel();
		return;
	}
	if (changes.marksChanged) {
		fDirty.marksChanged = true;
	}
	if (changes.edits.empty()) {
		return;
	}

	const Rope &text = fModel->Text();
	for (const EditRecord &edit : changes.edits) {
		fDisplayMap.ApplyEdit(edit);
		fSelection = MapSelection(fSelection, edit);
		fScrollAnchor = edit.MapOffset(fScrollAnchor, Gravity::Left);
		if (edit.oldEnd.line == edit.newEnd.line) {
			fDirty.Add(edit.start.line, edit.newEnd.line);
		} else {
			fDirty.Add(edit.start.line, SIZE_MAX);
			fDirty.scrollRangeChanged = true;
			fDirty.marksChanged = true;
		}
	}
	fLayoutCache.clear();

	// Later edits may have shifted the lines of earlier ones; the widest line
	// is only used for the scroll range, so an approximation is fine.
	for (const EditRecord &edit : changes.edits) {
		const size_t lastLine = std::min(edit.newEnd.line, text.NewlineCount());
		for (size_t line = std::min(edit.start.line, lastLine); line <= lastLine; line++) {
			const uint32_t width = LayoutOfLine(line).Width();
			if (width > fMaxColumns) {
				fMaxColumns = width;
				fDirty.scrollRangeChanged = true;
			}
		}
	}

	fSelection = ClampSelection(fSelection);
}


void EditorCore::TrimLayoutCache() const
{
	if (fLayoutCache.size() > kMaxCachedLayouts) {
		fLayoutCache.clear();
	}
}


std::string EditorCore::LineText(size_t line) const
{
	const Rope &text = fModel->Text();
	const size_t start = text.LineStart(line);
	return text.Substring(start, text.LineEnd(line) - start);
}


const LineLayout &EditorCore::LayoutOfLine(size_t line) const
{
	const auto it = fLayoutCache.find(line);
	if (it != fLayoutCache.end()) {
		return it->second;
	}
	return fLayoutCache.emplace(line, LineLayout(LineText(line), fLayoutOptions)).first->second;
}


size_t EditorCore::SnapToBoundary(size_t offset) const
{
	const Rope &text = fModel->Text();
	offset = std::min(offset, text.Length());
	const size_t line = text.LineOfOffset(offset);
	const size_t lineStart = text.LineStart(line);
	const LineLayout &layout = LayoutOfLine(line);
	if (layout.IsBoundary(offset - lineStart)) {
		return offset;
	}
	return lineStart + layout.NextBoundary(offset - lineStart);
}


size_t EditorCore::PreviousBoundary(size_t offset) const
{
	if (offset == 0) {
		return 0;
	}
	const Rope &text = fModel->Text();
	const size_t line = text.LineOfOffset(offset);
	const size_t lineStart = text.LineStart(line);
	if (offset == lineStart) {
		return offset - 1;
	}
	return lineStart + LayoutOfLine(line).PreviousBoundary(offset - lineStart);
}


size_t EditorCore::NextBoundary(size_t offset) const
{
	const Rope &text = fModel->Text();
	if (offset >= text.Length()) {
		return text.Length();
	}
	const size_t line = text.LineOfOffset(offset);
	const size_t lineStart = text.LineStart(line);
	if (offset >= text.LineEnd(line)) {
		return offset + 1;
	}
	return lineStart + LayoutOfLine(line).NextBoundary(offset - lineStart);
}


CaretPosition EditorCore::PositionOfOffsetLocked(size_t offset) const
{
	const Rope &text = fModel->Text();
	offset = std::min(offset, text.Length());
	const size_t line = text.LineOfOffset(offset);
	const uint32_t column = LayoutOfLine(line).ColumnOfOffset(offset - text.LineStart(line));
	return {fDisplayMap.FirstRowOfLine(line), column};
}


size_t EditorCore::OffsetAtLocked(size_t row, float column) const
{
	const DisplayRow displayRow = fDisplayMap.RowAt(std::min(row, fDisplayMap.RowCount() - 1));
	const size_t lineStart = fModel->Text().LineStart(displayRow.line);
	return lineStart + LayoutOfLine(displayRow.line).BoundaryNearColumn(column);
}


size_t EditorCore::SmartLineStart(size_t caret) const
{
	const Rope &text = fModel->Text();
	const size_t line = text.LineOfOffset(caret);
	const size_t lineStart = text.LineStart(line);
	if (!fSettings.smartHome) {
		return lineStart;
	}
	const size_t lineEnd = text.LineEnd(line);
	size_t firstNonBlank = lineStart;
	while (firstNonBlank < lineEnd) {
		const char ch = text.ByteAt(firstNonBlank);
		if (ch != ' ' && ch != '\t') {
			break;
		}
		firstNonBlank++;
	}
	return (caret > firstNonBlank || caret == lineStart) ? firstNonBlank : lineStart;
}


// Pe's page movement: first jump to the context line at the page edge, then by
// a page minus the context.
size_t EditorCore::PageTarget(Movement movement, size_t row, const PageInfo &page) const
{
	const size_t lastRow = fDisplayMap.RowCount() - 1;
	const size_t visible = std::max<size_t>(page.visibleRows, 1);
	const size_t context = std::min<size_t>(fSettings.contextLines, (visible - 1) / 2);
	const size_t step = std::max<size_t>(1, visible - 2 * context);

	if (movement == Movement::PageTop) {
		const size_t target = page.topRow + context;
		if (row > target) {
			return target;
		}
		return row > step ? row - step : 0;
	}

	const size_t target = page.topRow + visible - 1 - context;
	if (row < target) {
		return std::min(target, lastRow);
	}
	return std::min(row + step, lastRow);
}


Selection EditorCore::ClampSelection(const Selection &selection) const
{
	return {SnapToBoundary(selection.anchor), SnapToBoundary(selection.caret)};
}


void EditorCore::AddSelectionDirty(const Selection &selection)
{
	const size_t anchorRow = PositionOfOffsetLocked(selection.anchor).row;
	const size_t caretRow = PositionOfOffsetLocked(selection.caret).row;
	fDirty.Add(std::min(anchorRow, caretRow), std::max(anchorRow, caretRow));
}


void EditorCore::SetSelectionLocked(const Selection &selection)
{
	if (selection == fSelection) {
		return;
	}
	AddSelectionDirty(fSelection);
	fSelection = selection;
	AddSelectionDirty(fSelection);
}


void EditorCore::ReplaceRange(size_t from, size_t to, std::string_view text, EditKind kind)
{
	const size_t caretAfter = from + text.size();
	fModel->BeginGroup(fObserver, kind, fSelection);
	fModel->Replace(from, to - from, text);
	fModel->EndGroup({caretAfter, caretAfter});
	SyncLocked();
	fGoalColumn.reset();
	SetSelectionLocked(ClampSelection({caretAfter, caretAfter}));
}


//#pragma mark - Queries

void EditorCore::Prepare()
{
	TrimLayoutCache();
	SyncLocked();
}


Selection EditorCore::GetSelection()
{
	ModelLock lock(*fModel);
	Prepare();
	return fSelection;
}


size_t EditorCore::RowCount()
{
	ModelLock lock(*fModel);
	Prepare();
	return fDisplayMap.RowCount();
}


size_t EditorCore::LineCount()
{
	ModelLock lock(*fModel);
	Prepare();
	return fDisplayMap.LineCount();
}


uint32_t EditorCore::MaxColumns()
{
	ModelLock lock(*fModel);
	Prepare();
	return fMaxColumns;
}


std::vector<RowData> EditorCore::Rows(size_t first, size_t count)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	std::vector<RowData> rows;
	const size_t end = std::min(first + count, fDisplayMap.RowCount());
	for (size_t row = first; row < end; row++) {
		const DisplayRow displayRow = fDisplayMap.RowAt(row);
		RowData data;
		data.line = displayRow.line;
		data.lineStart = text.LineStart(displayRow.line);
		data.text = LineText(displayRow.line);
		data.hasNewline = displayRow.line < text.NewlineCount();
		data.layout = LayoutOfLine(displayRow.line);
		rows.push_back(std::move(data));
	}
	return rows;
}


std::vector<DisplayRow> EditorCore::DisplayRows(size_t first, size_t count)
{
	ModelLock lock(*fModel);
	Prepare();
	std::vector<DisplayRow> rows;
	const size_t end = std::min(first + count, fDisplayMap.RowCount());
	for (size_t row = first; row < end; row++) {
		rows.push_back(fDisplayMap.RowAt(row));
	}
	return rows;
}


CaretPosition EditorCore::PositionOfOffset(size_t offset)
{
	ModelLock lock(*fModel);
	Prepare();
	return PositionOfOffsetLocked(offset);
}


size_t EditorCore::OffsetAt(size_t row, float column)
{
	ModelLock lock(*fModel);
	Prepare();
	return OffsetAtLocked(row, column);
}


size_t EditorCore::RowStartOffset(size_t row)
{
	ModelLock lock(*fModel);
	Prepare();
	const DisplayRow displayRow = fDisplayMap.RowAt(std::min(row, fDisplayMap.RowCount() - 1));
	return fModel->Text().LineStart(displayRow.line);
}


std::string EditorCore::SelectedText()
{
	ModelLock lock(*fModel);
	Prepare();
	const size_t start = std::min(fSelection.anchor, fSelection.caret);
	const size_t end = std::max(fSelection.anchor, fSelection.caret);
	return fModel->Text().Substring(start, end - start);
}


CellHit EditorCore::HitTestCell(size_t row, float column)
{
	ModelLock lock(*fModel);
	Prepare();
	const DisplayRow displayRow = fDisplayMap.RowAt(std::min(row, fDisplayMap.RowCount() - 1));
	const size_t lineStart = fModel->Text().LineStart(displayRow.line);
	const LineLayout &layout = LayoutOfLine(displayRow.line);
	const size_t index = layout.ClusterAtColumn(static_cast<uint32_t>(std::max(0.0f, column)));
	if (index == layout.Clusters().size()) {
		return {lineStart + layout.Length(), true};
	}
	return {lineStart + layout.Clusters()[index].offset, false};
}


TextRange EditorCore::UnitRangeAt(size_t offset, SelectionUnit unit)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	offset = std::min(offset, text.Length());
	switch (unit) {
		case SelectionUnit::Word: {
			if (offset == text.Length() || text.ByteAt(offset) == '\n') {
				return {offset, offset};
			}
			auto isBlank = [&text](size_t pos) {
				const char ch = text.ByteAt(pos);
				return ch == ' ' || ch == '\t';
			};
			if (isBlank(offset)) {
				size_t start = offset;
				size_t end = offset + 1;
				while (start > 0 && isBlank(start - 1)) {
					start--;
				}
				while (end < text.Length() && isBlank(end)) {
					end++;
				}
				return {start, end};
			}
			const size_t next = NextBoundary(offset);
			const size_t start = FindWordBoundary(text, next, WordDirection::Backward, false, false);
			const size_t end = FindWordBoundary(text, offset, WordDirection::Forward, false, false);
			if (start > offset || end < next) {
				return {offset, next};
			}
			return {SnapToBoundary(start), SnapToBoundary(end)};
		}
		case SelectionUnit::Line: {
			const size_t line = text.LineOfOffset(offset);
			const size_t end = line < text.NewlineCount() ? text.LineStart(line + 1) : text.Length();
			return {text.LineStart(line), end};
		}
		default:
			break;
	}
	return {offset, offset};
}


static bool IsOpeningBracket(char ch)
{
	return ch == '(' || ch == '[' || ch == '{' || ch == '<';
}


static char PartnerBracket(char ch)
{
	switch (ch) {
		case '(':
			return ')';
		case '[':
			return ']';
		case '{':
			return '}';
		case '<':
			return '>';
		case ')':
			return '(';
		case ']':
			return '[';
		case '}':
			return '{';
		case '>':
			return '<';
		default:
			break;
	}
	return 0;
}


// TODO: skip brackets in strings and comments once syntax trees are available.
std::optional<TextRange> EditorCore::BalancedRangeAt(size_t offset)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	if (offset >= text.Length()) {
		return std::nullopt;
	}
	const char bracket = text.ByteAt(offset);
	const char partner = PartnerBracket(bracket);
	if (partner == 0) {
		return std::nullopt;
	}

	int depth = 0;
	if (IsOpeningBracket(bracket)) {
		for (size_t pos = offset + 1; pos < text.Length(); pos++) {
			const char ch = text.ByteAt(pos);
			if (ch == bracket) {
				depth++;
			} else if (ch == partner) {
				if (depth == 0) {
					return TextRange {offset + 1, pos};
				}
				depth--;
			}
		}
		return std::nullopt;
	}
	for (size_t pos = offset; pos > 0; pos--) {
		const char ch = text.ByteAt(pos - 1);
		if (ch == bracket) {
			depth++;
		} else if (ch == partner) {
			if (depth == 0) {
				return TextRange {pos, offset};
			}
			depth--;
		}
	}
	return std::nullopt;
}


void EditorCore::ExtendSelection(const TextRange &anchor, size_t offset, SelectionUnit unit)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	offset = std::min(offset, text.Length());

	Selection selection {anchor.start, anchor.end};
	if (offset < anchor.start) {
		size_t caret = offset;
		if (unit == SelectionUnit::Word) {
			caret = FindWordBoundary(text, offset, WordDirection::Backward, false, false);
		} else if (unit == SelectionUnit::Line) {
			caret = text.LineStart(text.LineOfOffset(offset));
		}
		selection = {anchor.end, caret};
	} else if (offset > anchor.end) {
		size_t caret = offset;
		if (unit == SelectionUnit::Word) {
			caret = FindWordBoundary(text, offset, WordDirection::Forward, false, false);
		} else if (unit == SelectionUnit::Line) {
			const size_t line = text.LineOfOffset(offset);
			caret = line < text.NewlineCount() ? text.LineStart(line + 1) : text.Length();
		}
		selection = {anchor.start, caret};
	}
	fGoalColumn.reset();
	SetSelectionLocked(ClampSelection(selection));
}


//#pragma mark - Commands

void EditorCore::Move(Movement movement, bool extend, const PageInfo &page)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t caret = fSelection.caret;
	const size_t selectionStart = std::min(fSelection.anchor, fSelection.caret);
	const size_t selectionEnd = std::max(fSelection.anchor, fSelection.caret);
	const bool collapse = !extend && selectionStart != selectionEnd;

	size_t newCaret = caret;
	std::optional<uint32_t> goalColumn;
	auto keepGoal = [&]() {
		goalColumn = fGoalColumn.value_or(PositionOfOffsetLocked(caret).column);
		return *goalColumn;
	};
	auto moveToRow = [&](size_t row) {
		return OffsetAtLocked(row, static_cast<float>(keepGoal()));
	};

	switch (movement) {
		case Movement::CharacterLeft:
			newCaret = collapse ? selectionStart : PreviousBoundary(caret);
			break;
		case Movement::CharacterRight:
			newCaret = collapse ? selectionEnd : NextBoundary(caret);
			break;
		case Movement::WordLeft:
		case Movement::SubwordLeft:
			newCaret = SnapToBoundary(FindWordBoundary(text, caret, WordDirection::Backward,
				movement == Movement::SubwordLeft));
			break;
		case Movement::WordRight:
		case Movement::SubwordRight:
			newCaret = SnapToBoundary(FindWordBoundary(text, caret, WordDirection::Forward,
				movement == Movement::SubwordRight));
			break;
		case Movement::LineStart:
			newCaret = SmartLineStart(caret);
			break;
		case Movement::LineEnd:
			newCaret = text.LineEnd(text.LineOfOffset(caret));
			break;
		case Movement::PreviousLine: {
			if (collapse) {
				newCaret = selectionStart;
				break;
			}
			const size_t row = PositionOfOffsetLocked(caret).row;
			if (row == 0) {
				keepGoal();
				newCaret = 0;
			} else {
				newCaret = moveToRow(row - 1);
			}
			break;
		}
		case Movement::NextLine: {
			if (collapse) {
				newCaret = selectionEnd;
				break;
			}
			const size_t row = PositionOfOffsetLocked(caret).row;
			if (row + 1 >= fDisplayMap.RowCount()) {
				keepGoal();
				newCaret = text.Length();
			} else {
				newCaret = moveToRow(row + 1);
			}
			break;
		}
		case Movement::PageTop:
		case Movement::PageBottom: {
			const size_t row = PositionOfOffsetLocked(caret).row;
			newCaret = moveToRow(PageTarget(movement, row, page));
			break;
		}
		case Movement::FileStart:
			newCaret = 0;
			break;
		case Movement::FileEnd:
			newCaret = text.Length();
			break;
		default:
			break;
	}

	fGoalColumn = goalColumn;
	fModel->BreakUndoCoalescing();
	SetSelectionLocked({extend ? fSelection.anchor : newCaret, newCaret});
}


void EditorCore::Delete(Deletion deletion)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t caret = fSelection.caret;
	const size_t selectionStart = std::min(fSelection.anchor, fSelection.caret);
	const size_t selectionEnd = std::max(fSelection.anchor, fSelection.caret);
	const bool hasSelection = selectionStart != selectionEnd;
	const size_t line = text.LineOfOffset(caret);

	size_t from = selectionStart;
	size_t to = selectionEnd;
	EditKind kind = EditKind::Other;
	switch (deletion) {
		case Deletion::CharacterLeft:
			if (!hasSelection) {
				from = PreviousBoundary(caret);
				to = caret;
				kind = EditKind::Backspace;
			}
			break;
		case Deletion::CharacterRight:
			if (!hasSelection) {
				from = caret;
				to = NextBoundary(caret);
				kind = EditKind::Delete;
			}
			break;
		case Deletion::ToLineStart:
			from = text.LineStart(line);
			to = caret;
			break;
		case Deletion::ToLineEnd:
			if (!hasSelection) {
				from = caret;
				to = text.LineEnd(line);
			}
			break;
		default:
			break;
	}
	if (from >= to) {
		return;
	}
	ReplaceRange(from, to, {}, kind);
}


void EditorCore::InsertText(std::string_view text)
{
	if (text.empty()) {
		return;
	}
	ModelLock lock(*fModel);
	Prepare();
	ReplaceRange(
		std::min(fSelection.anchor, fSelection.caret),
		std::max(fSelection.anchor, fSelection.caret),
		text,
		EditKind::Typing
	);
}


void EditorCore::InsertNewline()
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t from = std::min(fSelection.anchor, fSelection.caret);
	const size_t to = std::max(fSelection.anchor, fSelection.caret);

	std::string inserted = "\n";
	if (fSettings.autoIndent) {
		const size_t lineStart = text.LineStart(text.LineOfOffset(from));
		for (size_t pos = lineStart; pos < from; pos++) {
			const char ch = text.ByteAt(pos);
			if (ch != ' ' && ch != '\t') {
				break;
			}
			inserted += ch;
		}
	}
	ReplaceRange(from, to, inserted, EditKind::Typing);
}


void EditorCore::InsertTab()
{
	ModelLock lock(*fModel);
	Prepare();
	const size_t from = std::min(fSelection.anchor, fSelection.caret);
	const size_t to = std::max(fSelection.anchor, fSelection.caret);

	std::string inserted = "\t";
	if (fSettings.insertSpacesForTab) {
		const uint32_t column = PositionOfOffsetLocked(from).column;
		inserted.assign(fLayoutOptions.tabWidth - column % fLayoutOptions.tabWidth, ' ');
	}
	ReplaceRange(from, to, inserted, EditKind::Typing);
}


void EditorCore::Paste(std::string_view text)
{
	ModelLock lock(*fModel);
	Prepare();
	const size_t from = std::min(fSelection.anchor, fSelection.caret);
	const size_t to = std::max(fSelection.anchor, fSelection.caret);
	const std::string normalized = NormalizeLineEndings(text);
	if (normalized.empty() && from == to) {
		return;
	}
	ReplaceRange(from, to, normalized, EditKind::Other);
}


void EditorCore::DeleteSelection()
{
	ModelLock lock(*fModel);
	Prepare();
	const size_t from = std::min(fSelection.anchor, fSelection.caret);
	const size_t to = std::max(fSelection.anchor, fSelection.caret);
	if (from == to) {
		return;
	}
	ReplaceRange(from, to, {}, EditKind::Other);
}


void EditorCore::MoveText(const TextRange &source, size_t target)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	if (source.end > text.Length() || source.start >= source.end || target > text.Length()) {
		return;
	}
	if (target >= source.start && target <= source.end) {
		return;
	}
	const size_t length = source.end - source.start;
	const std::string moved = text.Substring(source.start, length);
	const size_t insertAt = target > source.end ? target - length : target;

	fModel->BeginGroup(fObserver, EditKind::Other, fSelection);
	fModel->Erase(source.start, length);
	fModel->Insert(insertAt, moved);
	fModel->EndGroup({insertAt, insertAt + length});
	SyncLocked();
	fGoalColumn.reset();
	SetSelectionLocked(ClampSelection({insertAt, insertAt + length}));
}


void EditorCore::InsertTextAt(size_t offset, std::string_view text)
{
	ModelLock lock(*fModel);
	Prepare();
	const std::string normalized = NormalizeLineEndings(text);
	offset = SnapToBoundary(offset);
	if (normalized.empty()) {
		return;
	}
	const size_t end = offset + normalized.size();
	fModel->BeginGroup(fObserver, EditKind::Other, fSelection);
	fModel->Insert(offset, normalized);
	fModel->EndGroup({offset, end});
	SyncLocked();
	fGoalColumn.reset();
	SetSelectionLocked(ClampSelection({offset, end}));
}


void EditorCore::SelectAll()
{
	ModelLock lock(*fModel);
	Prepare();
	fGoalColumn.reset();
	fModel->BreakUndoCoalescing();
	SetSelectionLocked({0, fModel->Text().Length()});
}


void EditorCore::SetSelection(const Selection &selection)
{
	ModelLock lock(*fModel);
	Prepare();
	fGoalColumn.reset();
	fModel->BreakUndoCoalescing();
	SetSelectionLocked(ClampSelection(selection));
}


void EditorCore::ApplyUndoResult(const std::optional<UndoResult> &result, const ModelChanges &changes)
{
	ApplyChanges(changes);
	fGoalColumn.reset();
	if (result->owner == fObserver) {
		SetSelectionLocked(ClampSelection(result->selection));
		return;
	}
	if (!changes.edits.empty()) {
		const size_t end = changes.edits.back().NewEnd();
		SetSelectionLocked(ClampSelection({end, end}));
	}
}


bool EditorCore::Undo()
{
	ModelLock lock(*fModel);
	Prepare();
	const std::optional<UndoResult> result = fModel->Undo();
	if (!result.has_value()) {
		return false;
	}
	ApplyUndoResult(result, fModel->Sync(fObserver));
	return true;
}


bool EditorCore::Redo()
{
	ModelLock lock(*fModel);
	Prepare();
	const std::optional<UndoResult> result = fModel->Redo();
	if (!result.has_value()) {
		return false;
	}
	ApplyUndoResult(result, fModel->Sync(fObserver));
	return true;
}


DirtyRows EditorCore::TakeDirty()
{
	const DirtyRows dirty = fDirty;
	fDirty = {};
	return dirty;
}


} // CodeEdit
