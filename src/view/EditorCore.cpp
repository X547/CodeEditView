#include "EditorCore.h"

#include <stdint.h>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string_view>

#include "WordMotion.h"
#include "model/DecorationMapping.h"
#include "syntax/Language.h"
#include "syntax/Outline.h"
#include "syntax/SyntaxState.h"


namespace CodeEdit {


static constexpr size_t kMaxCachedLayouts = 2048;
// About 12 MiB of clusters; a cache holding more (long lines) is emptied.
static constexpr size_t kMaxCachedClusters = 512 * 1024;
static constexpr size_t kMeasureLinesPerWrap = 8;
// More edits than this in one change are applied by laying out all lines.
static constexpr size_t kManyEdits = 64;
static constexpr size_t kMaxDirtyRanges = 8;
// Up to this many edits renumber the cached layouts, more empty the caches.
static constexpr size_t kMaxShiftedCacheEdits = 1024;


void DirtyRows::Add(size_t from, size_t to)
{
	first = std::min(first, from);
	last = std::max(last, to);

	// Ranges overlapping or touching the new one join it.
	auto it = ranges.begin();
	while (it != ranges.end() && it->second != SIZE_MAX && it->second + 1 < from) {
		++it;
	}
	while (it != ranges.end() && (to == SIZE_MAX || it->first <= to + 1)) {
		from = std::min(from, it->first);
		to = std::max(to, it->second);
		it = ranges.erase(it);
	}
	ranges.insert(it, {from, to});

	// The closest ranges are merged until few are left.
	while (ranges.size() > kMaxDirtyRanges) {
		size_t closest = 0;
		for (size_t i = 1; i + 1 < ranges.size(); i++) {
			if (ranges[i + 1].first - ranges[i].second < ranges[closest + 1].first - ranges[closest].second) {
				closest = i;
			}
		}
		ranges[closest].second = ranges[closest + 1].second;
		ranges.erase(ranges.begin() + closest + 1);
	}
}


void DirtyRows::AddShift(const RowShift &rowShift)
{
	if (!IsEmpty() || shift.has_value()) {
		// Rows dirty before moved too: everything after the edit is drawn
		// again.
		Add(rowShift.first, SIZE_MAX);
		shift.reset();
		marksChanged = true;
		return;
	}
	shift = rowShift;
	Add(rowShift.first, rowShift.from - 1);
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
	fMarkRevisions = MarkRevisions();
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
	ModelLock lock(*fModel);
	SyncLocked();

	LayoutOptions layoutOptions = fLayoutOptions;
	layoutOptions.tabWidth = std::max<uint32_t>(1, settings.tabWidth);
	layoutOptions.ambiguousWidth = std::clamp<uint32_t>(settings.ambiguousWidth, 1, 2);
	const bool relayout = layoutOptions.tabWidth != fLayoutOptions.tabWidth
		|| layoutOptions.ambiguousWidth != fLayoutOptions.ambiguousWidth
		|| settings.wordWrap != fSettings.wordWrap
		|| (settings.wordWrap && settings.wrapIndent != fSettings.wrapIndent);
	fSettings = settings;
	fLayoutOptions = layoutOptions;
	if (relayout) {
		Relayout();
		fSelection = ClampSelection(fSelection);
		fGoalColumn.reset();
	}
}


void EditorCore::SetWrapColumns(uint32_t columns)
{
	ModelLock lock(*fModel);
	Prepare();
	if (columns == fWrapColumns) {
		return;
	}
	const bool wasWrapping = IsWrappingLocked();
	fWrapColumns = columns;
	if (!fSettings.wordWrap) {
		return;
	}
	if (!wasWrapping || !IsWrappingLocked()) {
		Relayout();
		return;
	}
	// Lines are re-wrapped lazily, starting with the visible ones.
	fWrapCache.clear();
	const Rope &text = fModel->Text();
	fDisplayMap.MarkAllStale(text.LineOfOffset(std::min(fScrollAnchor, text.Length())));
	fDirty.AddAll();
	fDirty.marksChanged = true;
}


bool EditorCore::IsWrapping()
{
	ModelLock lock(*fModel);
	return IsWrappingLocked();
}


bool EditorCore::HasLayoutWork()
{
	ModelLock lock(*fModel);
	Prepare();
	return fDisplayMap.HasStaleLines() || (!IsWrappingLocked() && fMeasureLine < fModel->Text().LineCount());
}


// Measuring is cheaper than wrapping: more lines are measured at a time.
bool EditorCore::RewrapStep(size_t maxLines)
{
	ModelLock lock(*fModel);
	Prepare();
	for (size_t i = 0; i < maxLines; i++) {
		const std::optional<size_t> line = fDisplayMap.NextStaleLine();
		if (!line.has_value()) {
			break;
		}
		RewrapLine(*line);
	}
	const bool measuring = MeasureLines(kMeasureLinesPerWrap * maxLines);
	return fDisplayMap.HasStaleLines() || measuring;
}


void EditorCore::ResetFromModel()
{
	fFolds.clear();
	fHostRegions.clear();
	fDecorations.clear();
	Relayout();
	fDirty.contentsChanged = true;
	fSelection = ClampSelection(fSelection);
	fScrollAnchor = std::min(fScrollAnchor, fModel->Text().Length());
	fGoalColumn.reset();
}


// Lays out all lines again, keeping folds. Nothing is laid out at once:
// shown lines are laid out when drawn, the others in the background (see
// RewrapStep()).
void EditorCore::Relayout()
{
	const Rope &text = fModel->Text();
	fLayoutCache.clear();
	fWrapCache.clear();
	fDisplayMap.Reset(text.LineCount());
	fMaxColumns = 0;
	fMeasureLine = 0;
	if (IsWrappingLocked()) {
		fDisplayMap.MarkAllStale(text.LineOfOffset(std::min(fScrollAnchor, text.Length())));
	}
	ApplyFolds();
	fDirty.scrollRangeChanged = true;
}


// Lines are read chunk by chunk; only a line spanning chunks is copied.
bool EditorCore::MeasureLines(size_t maxLines)
{
	const Rope &text = fModel->Text();
	const size_t lineCount = text.LineCount();
	if (IsWrappingLocked() || fMeasureLine >= lineCount) {
		// Wrapped text has no horizontal extent; Relayout() starts again.
		fMeasureLine = lineCount;
		return false;
	}
	const size_t end = std::min(lineCount, fMeasureLine + maxLines);
	uint32_t widest = fMaxColumns;
	size_t pos = text.LineStart(fMeasureLine);
	std::string spanning;
	while (fMeasureLine < end) {
		const std::string_view chunk = text.ChunkAt(pos);
		const size_t newline = chunk.find('\n');
		if (newline == std::string_view::npos && !chunk.empty()) {
			spanning.append(chunk);
			pos += chunk.size();
			continue;
		}
		std::string_view line = chunk.substr(0, newline);
		if (!spanning.empty()) {
			spanning.append(line);
			line = spanning;
		}
		widest = std::max(widest, LineWidth(line, fLayoutOptions));
		spanning.clear();
		pos += chunk.empty() ? 0 : newline + 1;
		fMeasureLine++;
	}
	if (widest > fMaxColumns) {
		fMaxColumns = widest;
		fDirty.scrollRangeChanged = true;
	}
	return fMeasureLine < lineCount;
}


// Cached layouts of edited lines are dropped, the others renumbered.
void EditorCore::ShiftLayoutCaches(const EditRecord &edit)
{
	const size_t first = edit.start.line;
	const size_t oldLast = edit.oldEnd.line;
	const size_t newLast = edit.newEnd.line;
	const auto shift = [&](auto &cache) {
		if (oldLast == newLast) {
			for (size_t line = first; line <= oldLast; line++) {
				cache.erase(line);
			}
			return;
		}
		std::remove_reference_t<decltype(cache)> shifted;
		shifted.reserve(cache.size());
		for (auto &[line, value] : cache) {
			if (line < first) {
				shifted.emplace(line, std::move(value));
			} else if (line > oldLast) {
				shifted.emplace(line - oldLast + newLast, std::move(value));
			}
		}
		cache = std::move(shifted);
	};
	shift(fLayoutCache);
	shift(fWrapCache);

	if (fMeasureLine > oldLast) {
		fMeasureLine = fMeasureLine - oldLast + newLast;
	} else if (fMeasureLine > first) {
		fMeasureLine = first;
	}
}


void EditorCore::ApplyChanges(const ModelChanges &changes)
{
	if (changes.reset) {
		ResetFromModel();
		return;
	}
	if (changes.overlaysChanged) {
		CheckOverlays();
	}
	if (changes.syntaxChanged) {
		fDirty.AddAll();
		fDirty.contentsChanged = true;
	}
	if (changes.edits.empty()) {
		return;
	}
	fDirty.contentsChanged = true;
	if (changes.edits.size() > kManyEdits) {
		ApplyManyEdits(changes.edits);
		return;
	}

	const Rope &text = fModel->Text();
	// With one edit, the rows after it keep what they show: the view moves
	// them (DirtyRows::shift) instead of drawing them again.
	const bool single = changes.edits.size() == 1;
	const size_t oldEndRow = single ? fDisplayMap.FirstRowOfLine(changes.edits.front().oldEnd.line + 1) : 0;
	bool foldsRemoved = false;
	bool rowsChanged = false;
	// Inclusive line ranges touched by the edits, following later edits.
	std::vector<std::pair<size_t, size_t>> edited;
	for (const EditRecord &edit : changes.edits) {
		fDisplayMap.ApplyEdit(edit);
		ShiftLayoutCaches(edit);
		foldsRemoved = AdjustFolds(edit) || foldsRemoved;
		MapDecorations(edit);
		fSelection = MapSelection(fSelection, edit);
		fScrollAnchor = edit.MapOffset(fScrollAnchor, Gravity::Left);

		const size_t first = edit.start.line;
		const size_t oldLast = edit.oldEnd.line;
		const size_t newLast = edit.newEnd.line;
		rowsChanged = rowsChanged || oldLast != newLast;
		for (auto &[from, to] : edited) {
			if (from > oldLast) {
				from = from - oldLast + newLast;
				to = to - oldLast + newLast;
			} else if (to >= first) {
				from = std::min(from, first);
				to = to > oldLast ? to - oldLast + newLast : newLast;
			}
		}
		edited.emplace_back(first, newLast);
	}

	size_t firstLine = SIZE_MAX;
	size_t lastLine = 0;
	const bool wrapping = IsWrappingLocked();
	for (auto [from, to] : edited) {
		to = std::min(to, text.NewlineCount());
		firstLine = std::min(firstLine, from);
		lastLine = std::max(lastLine, to);
		for (size_t line = from; line <= to; line++) {
			const uint32_t width = LayoutOfLine(line).Width();
			if (width > fMaxColumns) {
				fMaxColumns = width;
				fDirty.scrollRangeChanged = true;
			}
			if (wrapping && UpdateWrapRows(line)) {
				rowsChanged = true;
			}
		}
	}

	if (foldsRemoved) {
		ApplyFolds();
	}
	const size_t firstRow = fDisplayMap.FirstRowOfLine(firstLine);
	const size_t newEndRow = single ? fDisplayMap.FirstRowOfLine(changes.edits.front().newEnd.line + 1) : 0;
	if (rowsChanged && single && !foldsRemoved && newEndRow > firstRow) {
		const EditRecord &edit = changes.edits.front();
		fDirty.AddShift({
			firstRow,
			newEndRow,
			static_cast<ptrdiff_t>(newEndRow) - static_cast<ptrdiff_t>(oldEndRow),
			edit.oldEnd.line != edit.newEnd.line
		});
		fDirty.scrollRangeChanged = true;
	} else if (rowsChanged) {
		fDirty.Add(firstRow, SIZE_MAX);
		fDirty.scrollRangeChanged = true;
		fDirty.marksChanged = true;
	} else {
		const size_t endRow = fDisplayMap.FirstRowOfLine(lastLine + 1);
		fDirty.Add(firstRow, endRow > firstRow ? endRow - 1 : firstRow);
	}
	fSelection = ClampSelection(fSelection);
}


// Many edits at once (an undo step of much typing, replace all): what follows
// offsets is mapped through each edit, and the edited lines are kept as one
// range. Without wrapping, the display map follows each edit cheaply (unless
// many edits change the line count of a map with line states); the lines
// are measured again in the background. Otherwise all lines are laid out
// again.
void EditorCore::ApplyManyEdits(const std::vector<EditRecord> &edits)
{
	const size_t lineCountChanges = std::count_if(edits.begin(), edits.end(), [](const EditRecord &edit) {
		return edit.oldEnd.line != edit.newEnd.line;
	});
	if (!IsWrappingLocked() && (!fDisplayMap.IsDense() || lineCountChanges <= kManyEdits)) {
		const bool shiftCaches = edits.size() <= kMaxShiftedCacheEdits;
		bool foldsRemoved = false;
		// Inclusive range of the edited lines, following later edits.
		size_t from = SIZE_MAX;
		size_t to = 0;
		for (const EditRecord &edit : edits) {
			fDisplayMap.ApplyEdit(edit);
			if (shiftCaches) {
				ShiftLayoutCaches(edit);
			}
			foldsRemoved = AdjustFolds(edit) || foldsRemoved;
			MapDecorations(edit);
			fSelection = MapSelection(fSelection, edit);
			fScrollAnchor = edit.MapOffset(fScrollAnchor, Gravity::Left);

			const size_t first = edit.start.line;
			const size_t oldLast = edit.oldEnd.line;
			const size_t newLast = edit.newEnd.line;
			if (from != SIZE_MAX) {
				if (from > oldLast) {
					from = from - oldLast + newLast;
					to = to - oldLast + newLast;
				} else if (to >= first) {
					to = to > oldLast ? to - oldLast + newLast : newLast;
				}
			}
			from = std::min(from, first);
			to = std::max(to, newLast);
		}
		if (!shiftCaches) {
			fLayoutCache.clear();
			fWrapCache.clear();
		}
		if (foldsRemoved) {
			ApplyFolds();
		}
		fMeasureLine = std::min(fMeasureLine, from);
		const size_t firstRow = fDisplayMap.FirstRowOfLine(from);
		if (lineCountChanges > 0 || foldsRemoved) {
			fDirty.Add(firstRow, SIZE_MAX);
			fDirty.scrollRangeChanged = true;
			fDirty.marksChanged = true;
		} else {
			const size_t lastLine = std::min(to, fModel->Text().NewlineCount());
			const size_t endRow = fDisplayMap.FirstRowOfLine(lastLine + 1);
			fDirty.Add(firstRow, endRow > firstRow ? endRow - 1 : firstRow);
		}
		fSelection = ClampSelection(fSelection);
		return;
	}

	for (const EditRecord &edit : edits) {
		AdjustFolds(edit);
		MapDecorations(edit);
		fSelection = MapSelection(fSelection, edit);
		fScrollAnchor = edit.MapOffset(fScrollAnchor, Gravity::Left);
	}
	Relayout();
	fSelection = ClampSelection(fSelection);
	fDirty.AddAll();
	fDirty.scrollRangeChanged = true;
	fDirty.marksChanged = true;
}


void EditorCore::TrimLayoutCache() const
{
	size_t clusters = 0;
	for (const auto &[line, layout] : fLayoutCache) {
		clusters += layout.Clusters().size();
	}
	if (fLayoutCache.size() > kMaxCachedLayouts || clusters > kMaxCachedClusters) {
		fLayoutCache.clear();
	}
	if (fWrapCache.size() > kMaxCachedLayouts) {
		fWrapCache.clear();
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
	const LineLayout &layout = fLayoutCache.emplace(line, LineLayout(LineText(line), fLayoutOptions)).first->second;
	if (layout.Width() > fMaxColumns) {
		fMaxColumns = layout.Width();
		fMaxColumnsGrew = true;
	}
	return layout;
}


//#pragma mark - Wrapping

std::vector<WrapRow> EditorCore::ComputeWrap(const LineLayout &layout) const
{
	return WrapLine(layout, IsWrappingLocked() ? fWrapColumns : 0, fSettings.wrapIndent);
}


const std::vector<WrapRow> &EditorCore::WrapOfLine(size_t line) const
{
	static const std::vector<WrapRow> kSingleRow {WrapRow {}};
	if (!IsWrappingLocked()) {
		return kSingleRow;
	}
	const auto it = fWrapCache.find(line);
	if (it != fWrapCache.end()) {
		return it->second;
	}
	return fWrapCache.emplace(line, ComputeWrap(LayoutOfLine(line))).first->second;
}


// Sets the row count of a line from its wrap; returns whether it changed.
bool EditorCore::UpdateWrapRows(size_t line)
{
	const uint32_t oldRows = fDisplayMap.WrapRows(line);
	uint32_t rows = 1;
	if (IsWrappingLocked()) {
		const auto it = fWrapCache.find(line);
		// Stale lines are mostly re-wrapped in bulk; do not fill the cache.
		rows = static_cast<uint32_t>(it != fWrapCache.end()
			? it->second.size()
			: ComputeWrap(LineLayout(LineText(line), fLayoutOptions)).size());
	}
	fDisplayMap.SetWrapRows(line, rows);
	return rows != oldRows && !fDisplayMap.IsHidden(line);
}


// Re-wraps a line outside of an edit; returns whether its row count changed.
// Text at the scroll anchor stays in place (see CodeEditView::ApplyDirty()),
// so only changes from the anchor line on need repainting.
bool EditorCore::RewrapLine(size_t line)
{
	if (!UpdateWrapRows(line)) {
		return false;
	}
	fDirty.scrollRangeChanged = true;
	const Rope &text = fModel->Text();
	if (line >= text.LineOfOffset(std::min(fScrollAnchor, text.Length()))) {
		fDirty.Add(fDisplayMap.FirstRowOfLine(line), SIZE_MAX);
	}
	return true;
}


void EditorCore::EnsureWrapped(size_t line)
{
	if (fDisplayMap.IsStale(line)) {
		RewrapLine(line);
	}
}


bool EditorCore::WrapRowsLocked(size_t firstRow, size_t count)
{
	bool changed = false;
	size_t row = firstRow;
	// Re-wrapping a line changes the rows after its first one, so the same
	// row is looked up again.
	while (fDisplayMap.HasStaleLines() && row < firstRow + count && row < fDisplayMap.RowCount()) {
		const DisplayRow displayRow = fDisplayMap.RowAt(row);
		if (fDisplayMap.IsStale(displayRow.line)) {
			changed = RewrapLine(displayRow.line) || changed;
			continue;
		}
		row = fDisplayMap.FirstRowOfLine(displayRow.line + 1);
	}
	return changed;
}


bool EditorCore::WrapRows(size_t firstRow, size_t count)
{
	ModelLock lock(*fModel);
	Prepare();
	return WrapRowsLocked(firstRow, count);
}


void EditorCore::WrapFromScrollAnchor(size_t count)
{
	ModelLock lock(*fModel);
	Prepare();
	if (!fDisplayMap.HasStaleLines()) {
		return;
	}
	const Rope &text = fModel->Text();
	EnsureWrapped(text.LineOfOffset(std::min(fScrollAnchor, text.Length())));
	WrapRowsLocked(PositionOfOffsetLocked(fScrollAnchor).row, count);
}


// Stale lines may have a different number of rows than their wrap; rows
// past the wrap repeat its last row.
DisplayRow EditorCore::RowAtLocked(size_t row) const
{
	return fDisplayMap.RowAt(std::min(row, fDisplayMap.RowCount() - 1));
}


RowSpan EditorCore::SpanOf(size_t line, uint32_t subRow) const
{
	const std::vector<WrapRow> &wrap = WrapOfLine(line);
	RowSpan span;
	span.line = line;
	span.lineStart = fModel->Text().LineStart(line);
	span.subRow = std::min<uint32_t>(subRow, static_cast<uint32_t>(wrap.size() - 1));
	span.row = wrap[span.subRow];
	span.last = span.subRow + 1 == wrap.size();
	span.end = span.last ? LayoutOfLine(line).Length() : wrap[span.subRow + 1].offset;
	return span;
}


RowSpan EditorCore::SpanOfOffset(size_t offset) const
{
	const Rope &text = fModel->Text();
	offset = std::min(offset, text.Length());
	const size_t line = text.LineOfOffset(offset);
	const std::vector<WrapRow> &wrap = WrapOfLine(line);
	const size_t inLine = offset - text.LineStart(line);
	const auto next = std::upper_bound(wrap.begin(), wrap.end(), inLine, [](size_t value, const WrapRow &row) {
		return value < row.offset;
	});
	return SpanOf(line, static_cast<uint32_t>(next - wrap.begin() - 1));
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
	offset = std::min(offset, fModel->Text().Length());
	const RowSpan span = SpanOfOffset(offset);
	const uint32_t column = LayoutOfLine(span.line).ColumnOfOffset(offset - span.lineStart)
		- span.row.column + span.row.indent;
	const uint32_t subRow = fDisplayMap.IsHidden(span.line)
		? 0
		: std::min(span.subRow, fDisplayMap.WrapRows(span.line) - 1);
	return {fDisplayMap.FirstRowOfLine(span.line) + subRow, column};
}


// Column of an offset in its line, independent of wrapping.
uint32_t EditorCore::LineColumnOfOffset(size_t offset) const
{
	const Rope &text = fModel->Text();
	offset = std::min(offset, text.Length());
	const size_t line = text.LineOfOffset(offset);
	return LayoutOfLine(line).ColumnOfOffset(offset - text.LineStart(line));
}


// The last offset of a row that is not the last of its line is before its
// last cluster; the end of the row is the start of the next one.
size_t EditorCore::OffsetAtLocked(size_t row, float column) const
{
	const DisplayRow displayRow = RowAtLocked(row);
	const RowSpan span = SpanOf(displayRow.line, displayRow.subRow);
	const LineLayout &layout = LayoutOfLine(span.line);
	const float lineColumn = column - span.row.indent + span.row.column;
	const size_t last = span.last ? layout.Length() : layout.PreviousBoundary(span.end);
	const size_t offset = std::clamp<size_t>(layout.BoundaryNearColumn(lineColumn), span.row.offset, last);
	return span.lineStart + offset;
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
	if (!fFolds.empty()) {
		const Rope &text = fModel->Text();
		const size_t anchorLine = text.LineOfOffset(selection.anchor);
		const size_t caretLine = text.LineOfOffset(selection.caret);
		const bool anchorUnfolded = UnfoldLines(anchorLine, anchorLine);
		const bool caretUnfolded = UnfoldLines(caretLine, caretLine);
		if (anchorUnfolded || caretUnfolded) {
			ApplyFolds();
		}
	}
	if (selection == fSelection) {
		return;
	}
	if (selection.anchor == fSelection.anchor) {
		// Only the caret end moved (extending a selection, moving the caret):
		// the rows between its places changed.
		AddSelectionDirty({fSelection.caret, selection.caret});
	} else {
		AddSelectionDirty(fSelection);
		AddSelectionDirty(selection);
	}
	fSelection = selection;
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
	for (size_t row = first; row < first + count && row < fDisplayMap.RowCount(); row++) {
		const DisplayRow displayRow = RowAtLocked(row);
		const RowSpan span = SpanOf(displayRow.line, displayRow.subRow);
		RowData data;
		data.line = displayRow.line;
		data.lineStart = span.lineStart;
		if (!rows.empty() && rows.back().line == displayRow.line) {
			data.text = rows.back().text;
			data.layout = rows.back().layout;
		} else {
			data.text = LineText(displayRow.line);
			data.layout = LayoutOfLine(displayRow.line);
		}
		data.hasNewline = displayRow.line < text.NewlineCount();
		data.subRow = span.subRow;
		data.lastRow = span.last;
		data.rowStart = span.row.offset;
		data.rowEnd = span.end;
		data.rowColumn = span.row.column;
		data.indent = span.row.indent;
		rows.push_back(std::move(data));
	}

	const SyntaxState *syntax = fModel->Syntax();
	if (rows.empty() || syntax == nullptr || syntax->Tree() == nullptr) {
		return rows;
	}
	const size_t start = rows.front().lineStart;
	std::vector<HighlightStyle> styles(rows.back().lineStart + rows.back().text.size() - start);
	Highlight(syntax->GetLanguage(), syntax->Tree(), text, fQueryCursor, start, styles);
	for (RowData &data : rows) {
		const auto first = styles.begin() + (data.lineStart - start);
		data.styles.assign(first, first + data.text.size());
	}
	return rows;
}


std::vector<DisplayRow> EditorCore::DisplayRows(size_t first, size_t count)
{
	ModelLock lock(*fModel);
	Prepare();
	std::vector<DisplayRow> rows;
	for (size_t row = first; row < first + count && row < fDisplayMap.RowCount(); row++) {
		rows.push_back(RowAtLocked(row));
	}
	return rows;
}


CaretPosition EditorCore::PositionOfOffset(size_t offset)
{
	ModelLock lock(*fModel);
	Prepare();
	return PositionOfOffsetLocked(offset);
}


std::pair<size_t, size_t> EditorCore::LineRows(size_t line)
{
	ModelLock lock(*fModel);
	Prepare();
	if (line >= fDisplayMap.LineCount()) {
		return {fDisplayMap.RowCount(), 0};
	}
	const size_t first = fDisplayMap.FirstRowOfLine(line);
	return {first, fDisplayMap.FirstRowOfLine(line + 1) - first};
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
	const DisplayRow displayRow = RowAtLocked(row);
	const RowSpan span = SpanOf(displayRow.line, displayRow.subRow);
	return span.lineStart + span.row.offset;
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
	const DisplayRow displayRow = RowAtLocked(row);
	const RowSpan span = SpanOf(displayRow.line, displayRow.subRow);
	const LineLayout &layout = LayoutOfLine(span.line);
	const float rowColumn = std::max(0.0f, column - span.row.indent);
	const size_t index = layout.ClusterAtColumn(static_cast<uint32_t>(rowColumn) + span.row.column);
	if (index == layout.Clusters().size() || layout.Clusters()[index].offset >= span.end) {
		if (span.last) {
			return {span.lineStart + layout.Length(), true};
		}
		// Past the end of a wrapped row: its last cluster.
		return {span.lineStart + layout.PreviousBoundary(span.end), false};
	}
	return {span.lineStart + layout.Clusters()[index].offset, false};
}


TextPoint EditorCore::CaretLocation()
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t line = text.LineOfOffset(fSelection.caret);
	return {line, LayoutOfLine(line).ColumnOfOffset(fSelection.caret - text.LineStart(line))};
}


size_t EditorCore::OffsetOfLocation(const TextPoint &location)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t line = std::min(location.line, text.NewlineCount());
	const LineLayout &layout = LayoutOfLine(line);
	const size_t cluster = layout.ClusterAtColumn(static_cast<uint32_t>(std::min<size_t>(location.column,
		UINT32_MAX)));
	const size_t offset = cluster < layout.Clusters().size() ? layout.Clusters()[cluster].offset : layout.Length();
	return text.LineStart(line) + offset;
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


static bool IsNodeType(TSNode node, char ch)
{
	return !ts_node_is_named(node) && std::string_view(ts_node_type(node)) == std::string_view(&ch, 1);
}


std::optional<TextRange> EditorCore::BalancedRangeAt(size_t offset)
{
	ModelLock lock(*fModel);
	Prepare();
	return BalancedRangeAtLocked(offset);
}


// With a syntax tree only bracket tokens match, so brackets in strings and
// comments are ignored, and the partner is the sibling token. Otherwise, and
// when there is no such sibling (syntax errors), brackets are matched by
// counting bytes.
std::optional<TextRange> EditorCore::BalancedRangeAtLocked(size_t offset) const
{
	const Rope &text = fModel->Text();
	if (offset >= text.Length()) {
		return std::nullopt;
	}
	const char bracket = text.ByteAt(offset);
	const char partner = PartnerBracket(bracket);
	if (partner == 0) {
		return std::nullopt;
	}

	const SyntaxState *syntax = fModel->Syntax();
	if (syntax != nullptr && syntax->Tree() != nullptr) {
		const uint32_t start = static_cast<uint32_t>(offset);
		const TSNode node = ts_node_descendant_for_byte_range(ts_tree_root_node(syntax->Tree()), start, start + 1);
		if (ts_node_is_null(node) || !IsNodeType(node, bracket) || ts_node_start_byte(node) != start) {
			return std::nullopt;
		}
		const TSNode parent = ts_node_parent(node);
		if (ts_node_is_null(parent)) {
			return std::nullopt;
		}
		const uint32_t childCount = ts_node_child_count(parent);
		uint32_t index = 0;
		while (index < childCount && !ts_node_eq(ts_node_child(parent, index), node)) {
			index++;
		}
		if (IsOpeningBracket(bracket)) {
			for (uint32_t i = index + 1; i < childCount; i++) {
				const TSNode child = ts_node_child(parent, i);
				if (IsNodeType(child, partner)) {
					return TextRange {offset + 1, ts_node_start_byte(child)};
				}
			}
		} else {
			for (uint32_t i = index; i > 0; i--) {
				const TSNode child = ts_node_child(parent, i - 1);
				if (IsNodeType(child, partner)) {
					return TextRange {ts_node_end_byte(child), offset};
				}
			}
		}
		// In code with syntax errors the partner may not be a sibling; fall
		// back to counting bytes.
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
	// Commands are followed by CodeEditView::ApplyDirty(), so re-wrapping
	// is safe here.
	EnsureWrapped(text.LineOfOffset(caret));

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
		// In a wrapped line, the start and end of the row come first.
		case Movement::LineStart: {
			const RowSpan span = SpanOfOffset(caret);
			const size_t rowStart = span.lineStart + span.row.offset;
			newCaret = span.subRow > 0 && caret != rowStart ? rowStart : SmartLineStart(caret);
			break;
		}
		case Movement::LineEnd: {
			const RowSpan span = SpanOfOffset(caret);
			const size_t rowEnd = span.lineStart + LayoutOfLine(span.line).PreviousBoundary(span.end);
			newCaret = !span.last && caret != rowEnd ? rowEnd : text.LineEnd(span.line);
			break;
		}
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

	// Horizontal movement jumps over folded lines; other movements into
	// them unfold (in SetSelectionLocked()).
	if (const std::optional<FoldRange> fold = HidingFold(text.LineOfOffset(newCaret))) {
		switch (movement) {
			case Movement::CharacterLeft:
			case Movement::CharacterRight:
			case Movement::WordLeft:
			case Movement::WordRight:
			case Movement::SubwordLeft:
			case Movement::SubwordRight:
				if (newCaret > caret) {
					newCaret = fold->lastLine < text.NewlineCount() ? text.LineStart(fold->lastLine + 1) : text.Length();
				} else {
					newCaret = text.LineEnd(fold->headerLine);
				}
				break;
			default:
				break;
		}
	}

	fGoalColumn = goalColumn;
	fModel->BreakUndoCoalescing();
	SetSelectionLocked({extend ? fSelection.anchor : newCaret, newCaret});
}


void EditorCore::Delete(Deletion deletion)
{
	ModelLock lock(*fModel);
	Prepare();
	if (IsReadOnly()) {
		return;
	}
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
	if (text.empty() || IsReadOnly()) {
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
	if (fSettings.hardWrapColumn > 0 && text.find('\n') == std::string_view::npos) {
		HardWrapCaretLine();
	}
}


static bool IsBlank(char ch)
{
	return ch == ' ' || ch == '\t';
}


void EditorCore::HardWrapCaretLine()
{
	const Rope &text = fModel->Text();
	const size_t caret = fSelection.caret;
	const size_t line = text.LineOfOffset(caret);
	const uint32_t limit = fSettings.hardWrapColumn;
	if (line < fSettings.hardWrapFirstLine || LineColumnOfOffset(caret) <= limit) {
		return;
	}

	// The leading blanks are kept and copied to the new line.
	const size_t lineStart = text.LineStart(line);
	size_t contentStart = lineStart;
	while (contentStart < caret && IsBlank(text.ByteAt(contentStart))) {
		contentStart++;
	}
	// The last blank run before the caret that starts within the limit.
	std::optional<std::pair<size_t, size_t>> blanks;
	for (size_t pos = contentStart; pos < caret;) {
		if (!IsBlank(text.ByteAt(pos))) {
			pos++;
			continue;
		}
		size_t end = pos;
		while (end < caret && IsBlank(text.ByteAt(end))) {
			end++;
		}
		if (end < caret && LineColumnOfOffset(pos) <= limit) {
			blanks = std::pair(pos, end);
		}
		pos = end;
	}
	if (!blanks.has_value()) {
		return;
	}

	std::string inserted = "\n";
	if (fSettings.autoIndent) {
		for (size_t pos = lineStart; pos < contentStart; pos++) {
			inserted += text.ByteAt(pos);
		}
	}
	const auto [from, to] = *blanks;
	const size_t caretAfter = caret - (to - from) + inserted.size();
	fModel->BeginGroup(fObserver, EditKind::Typing, fSelection);
	fModel->Replace(from, to - from, inserted);
	fModel->EndGroup({caretAfter, caretAfter});
	SyncLocked();
	SetSelectionLocked(ClampSelection({caretAfter, caretAfter}));
}


void EditorCore::InsertNewline()
{
	ModelLock lock(*fModel);
	Prepare();
	if (IsReadOnly()) {
		return;
	}
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
	if (IsReadOnly()) {
		return;
	}
	const size_t from = std::min(fSelection.anchor, fSelection.caret);
	const size_t to = std::max(fSelection.anchor, fSelection.caret);

	std::string inserted = "\t";
	if (fSettings.insertSpacesForTab) {
		const uint32_t column = LineColumnOfOffset(from);
		inserted.assign(IndentSize() - column % IndentSize(), ' ');
	}
	ReplaceRange(from, to, inserted, EditKind::Typing);
}


uint32_t EditorCore::IndentSize() const
{
	return fSettings.indentSize > 0 ? fSettings.indentSize : fLayoutOptions.tabWidth;
}


void EditorCore::ChangeIndent(bool increase)
{
	ModelLock lock(*fModel);
	Prepare();
	if (IsReadOnly()) {
		return;
	}
	const Rope &text = fModel->Text();
	const size_t start = std::min(fSelection.anchor, fSelection.caret);
	const size_t end = std::max(fSelection.anchor, fSelection.caret);
	const size_t firstLine = text.LineOfOffset(start);
	size_t lastLine = text.LineOfOffset(end);
	if (lastLine > firstLine && end == text.LineStart(lastLine)) {
		lastLine--;
	}

	const std::string unit = fSettings.insertSpacesForTab ? std::string(IndentSize(), ' ') : std::string("\t");
	fModel->BeginGroup(fObserver, EditKind::Other, fSelection);
	// Last line first, so the offsets of earlier lines stay valid.
	for (size_t line = lastLine + 1; line-- > firstLine;) {
		const size_t lineStart = text.LineStart(line);
		const size_t lineEnd = text.LineEnd(line);
		if (lineStart == lineEnd) {
			continue;
		}
		if (increase) {
			fModel->Insert(lineStart, unit);
			continue;
		}
		size_t length = 0;
		if (text.ByteAt(lineStart) == '\t') {
			length = 1;
		} else {
			while (length < IndentSize() && lineStart + length < lineEnd && text.ByteAt(lineStart + length) == ' ') {
				length++;
			}
		}
		if (length > 0) {
			fModel->Erase(lineStart, length);
		}
	}
	// The selection follows the edits (see MapSelection()).
	SyncLocked();
	fGoalColumn.reset();
	fModel->EndGroup(fSelection);
}


void EditorCore::Paste(std::string_view text)
{
	ModelLock lock(*fModel);
	Prepare();
	if (IsReadOnly()) {
		return;
	}
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
	if (IsReadOnly()) {
		return;
	}
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
	if (IsReadOnly()) {
		return;
	}
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
	if (IsReadOnly()) {
		return;
	}
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


void EditorCore::UpdateBracketMatch()
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t caret = fSelection.caret;

	std::optional<BracketPair> match;
	if (fSelection.anchor == caret) {
		for (const size_t pos : {caret, caret - 1}) {
			if (pos >= text.Length()) {
				continue;
			}
			const char ch = text.ByteAt(pos);
			if (ch != '(' && ch != ')' && ch != '[' && ch != ']' && ch != '{' && ch != '}') {
				continue;
			}
			const std::optional<TextRange> range = BalancedRangeAtLocked(pos);
			if (range.has_value()) {
				match = BracketPair {pos, IsOpeningBracket(ch) ? range->end : range->start - 1};
				break;
			}
		}
	}
	if (match == fBracketMatch) {
		return;
	}
	for (const std::optional<BracketPair> &pair : {fBracketMatch, match}) {
		if (pair.has_value()) {
			const size_t bracketRow = PositionOfOffsetLocked(pair->bracket).row;
			const size_t partnerRow = PositionOfOffsetLocked(pair->partner).row;
			fDirty.Add(std::min(bracketRow, partnerRow), std::max(bracketRow, partnerRow));
		}
	}
	fBracketMatch = match;
}


// Candidate ranges of a node for expanding a selection: the contents between
// its brackets (like Pe's Balance command), then the whole node.
static std::vector<TextRange> ExpansionCandidates(TSNode node)
{
	std::vector<TextRange> candidates;
	const uint32_t childCount = ts_node_child_count(node);
	if (childCount >= 2) {
		const TSNode first = ts_node_child(node, 0);
		const TSNode last = ts_node_child(node, childCount - 1);
		const std::string_view firstType = ts_node_type(first);
		if (!ts_node_is_named(first) && firstType.size() == 1 && IsOpeningBracket(firstType[0])
			&& IsNodeType(last, PartnerBracket(firstType[0]))) {
			candidates.push_back({ts_node_end_byte(first), ts_node_start_byte(last)});
		}
	}
	candidates.push_back({ts_node_start_byte(node), ts_node_end_byte(node)});
	return candidates;
}


void EditorCore::ExpandSelection()
{
	ModelLock lock(*fModel);
	Prepare();
	const size_t start = std::min(fSelection.anchor, fSelection.caret);
	const size_t end = std::max(fSelection.anchor, fSelection.caret);
	if (fExpandedSelection != fSelection) {
		fExpandHistory.clear();
	}

	const SyntaxState *syntax = fModel->Syntax();
	if (syntax == nullptr || syntax->Tree() == nullptr) {
		return;
	}
	TextRange target {0, fModel->Text().Length()};
	TSNode node = ts_node_descendant_for_byte_range(
		ts_tree_root_node(syntax->Tree()),
		static_cast<uint32_t>(start),
		static_cast<uint32_t>(end)
	);
	bool found = false;
	while (!ts_node_is_null(node) && !found) {
		for (const TextRange &candidate : ExpansionCandidates(node)) {
			if (candidate.start <= start && candidate.end >= end && candidate.end - candidate.start > end - start) {
				target = candidate;
				found = true;
				break;
			}
		}
		node = ts_node_parent(node);
	}
	if (target.start == start && target.end == end) {
		return;
	}

	fExpandHistory.push_back(fSelection);
	fGoalColumn.reset();
	fModel->BreakUndoCoalescing();
	SetSelectionLocked(ClampSelection({target.start, target.end}));
	fExpandedSelection = fSelection;
}


void EditorCore::ShrinkSelection()
{
	ModelLock lock(*fModel);
	Prepare();
	if (fExpandHistory.empty() || fExpandedSelection != fSelection) {
		return;
	}
	const Selection previous = fExpandHistory.back();
	fExpandHistory.pop_back();
	fGoalColumn.reset();
	SetSelectionLocked(ClampSelection(previous));
	fExpandedSelection = fExpandHistory.empty() ? std::nullopt : std::optional<Selection>(fSelection);
}


void EditorCore::MoveToDefinition(bool forward)
{
	ModelLock lock(*fModel);
	Prepare();
	const SyntaxState *syntax = fModel->Syntax();
	if (syntax == nullptr || syntax->Tree() == nullptr) {
		return;
	}
	const std::vector<size_t> starts = DefinitionStarts(syntax->GetLanguage(), syntax->Tree(), fQueryCursor);
	const size_t caret = fSelection.caret;
	std::optional<size_t> target;
	if (forward) {
		const auto next = std::upper_bound(starts.begin(), starts.end(), caret);
		if (next != starts.end()) {
			target = *next;
		}
	} else {
		const auto next = std::lower_bound(starts.begin(), starts.end(), caret);
		if (next != starts.begin()) {
			target = *(next - 1);
		}
	}
	if (!target.has_value()) {
		return;
	}
	fGoalColumn.reset();
	fModel->BreakUndoCoalescing();
	SetSelectionLocked(ClampSelection({*target, *target}));
}


std::vector<OutlineItem> EditorCore::Outline()
{
	ModelLock lock(*fModel);
	Prepare();
	return fModel->Outline();
}


std::vector<MarkInfo> EditorCore::Marks(size_t firstLine, size_t lastLine)
{
	ModelLock lock(*fModel);
	Prepare();
	std::vector<MarkInfo> marks = fModel->SharedMarks().Marks(firstLine, lastLine);
	for (const BReference<MarkSet> &set : fMarkSets) {
		const std::vector<MarkInfo> setMarks = set->Marks(firstLine, lastLine);
		marks.insert(marks.end(), setMarks.begin(), setMarks.end());
	}
	std::stable_sort(marks.begin(), marks.end(), [](const MarkInfo &a, const MarkInfo &b) {
		return a.line < b.line;
	});
	return marks;
}


size_t EditorCore::RevealLine(size_t line)
{
	ModelLock lock(*fModel);
	Prepare();
	line = std::min(line, fModel->Text().NewlineCount());
	if (UnfoldLines(line, line)) {
		ApplyFolds();
	}
	EnsureWrapped(line);
	return fDisplayMap.FirstRowOfLine(line);
}


//#pragma mark - Folding

void EditorCore::ApplyFolds()
{
	std::vector<std::pair<size_t, size_t>> hidden;
	for (const FoldRange &fold : fFolds) {
		hidden.emplace_back(fold.headerLine + 1, fold.lastLine);
	}
	fDisplayMap.SetHiddenLines(hidden);
	fDirty.AddAll();
	fDirty.scrollRangeChanged = true;
	fDirty.marksChanged = true;
}


// Keeps fold ranges valid across an edit: ranges after it shift, edits
// within a header line keep the range, other edits touching it remove it.
static bool AdjustFoldRanges(std::vector<FoldRange> &ranges, const EditRecord &edit)
{
	const size_t first = edit.start.line;
	const size_t oldLast = edit.oldEnd.line;
	const size_t newLast = edit.newEnd.line;
	bool removed = false;
	for (auto it = ranges.begin(); it != ranges.end();) {
		FoldRange &fold = *it;
		if (oldLast < fold.headerLine) {
			fold.headerLine = fold.headerLine - oldLast + newLast;
			fold.lastLine = fold.lastLine - oldLast + newLast;
		} else if (first > fold.lastLine) {
			// After the fold.
		} else if (first == fold.headerLine && oldLast == fold.headerLine && newLast == fold.headerLine) {
			// Within the header line.
		} else {
			it = ranges.erase(it);
			removed = true;
			continue;
		}
		++it;
	}
	return removed;
}


bool EditorCore::AdjustFolds(const EditRecord &edit)
{
	AdjustFoldRanges(fHostRegions, edit);
	return AdjustFoldRanges(fFolds, edit);
}


// The outermost fold hiding a line.
std::optional<FoldRange> EditorCore::HidingFold(size_t line) const
{
	for (const FoldRange &fold : fFolds) {
		if (fold.headerLine < line && line <= fold.lastLine) {
			return fold;
		}
	}
	return std::nullopt;
}


bool EditorCore::UnfoldLines(size_t first, size_t last)
{
	const size_t count = std::erase_if(fFolds, [first, last](const FoldRange &fold) {
		return first <= fold.lastLine && last > fold.headerLine;
	});
	return count > 0;
}


bool EditorCore::IsFoldedLocked(size_t headerLine) const
{
	return std::any_of(fFolds.begin(), fFolds.end(), [headerLine](const FoldRange &fold) {
		return fold.headerLine == headerLine;
	});
}


void EditorCore::AddFold(const FoldRange &fold)
{
	const Rope &text = fModel->Text();
	if (fold.lastLine <= fold.headerLine || fold.lastLine > text.NewlineCount()) {
		return;
	}
	std::erase_if(fFolds, [&fold](const FoldRange &other) {
		return other.headerLine == fold.headerLine;
	});
	fFolds.insert(
		std::upper_bound(fFolds.begin(), fFolds.end(), fold, [](const FoldRange &a, const FoldRange &b) {
			return a.headerLine < b.headerLine;
		}),
		fold
	);
	ApplyFolds();

	// A hidden caret moves to the end of the header line.
	for (const size_t offset : {fSelection.anchor, fSelection.caret}) {
		if (const std::optional<FoldRange> hiding = HidingFold(text.LineOfOffset(offset))) {
			const size_t headerEnd = text.LineEnd(hiding->headerLine);
			fGoalColumn.reset();
			SetSelectionLocked({headerEnd, headerEnd});
			break;
		}
	}
}


size_t EditorCore::FirstNonBlank(size_t line) const
{
	const Rope &text = fModel->Text();
	size_t offset = text.LineStart(line);
	const size_t end = text.LineEnd(line);
	while (offset < end && (text.ByteAt(offset) == ' ' || text.ByteAt(offset) == '\t')) {
		offset++;
	}
	return offset;
}


// Fold candidates from the syntax tree, merged per header line (the largest
// range wins). If a node ends with a token that starts its last line, like
// a closing brace or #endif, that line stays visible.
std::vector<FoldRange> EditorCore::FoldCandidatesLocked(size_t firstLine, size_t lastLine, bool headersInRange)
{
	std::vector<FoldRange> candidates;
	const Rope &text = fModel->Text();
	lastLine = std::min(lastLine, text.NewlineCount());
	if (firstLine > lastLine) {
		return candidates;
	}
	// Host regions come first, so they win over syntax candidates of the
	// same size and header line.
	for (const FoldRange &region : fHostRegions) {
		const bool inRange = headersInRange
			? region.headerLine >= firstLine && region.headerLine <= lastLine
			: region.headerLine <= lastLine && region.lastLine >= firstLine;
		if (inRange) {
			candidates.push_back(region);
		}
	}
	const SyntaxState *syntax = fModel->Syntax();
	if (syntax != nullptr && syntax->Tree() != nullptr) {
		AddSyntaxFoldCandidates(syntax, firstLine, lastLine, headersInRange, candidates);
	}

	std::stable_sort(candidates.begin(), candidates.end(), [](const FoldRange &a, const FoldRange &b) {
		return a.headerLine != b.headerLine ? a.headerLine < b.headerLine : a.lastLine > b.lastLine;
	});
	candidates.erase(
		std::unique(candidates.begin(), candidates.end(), [](const FoldRange &a, const FoldRange &b) {
			return a.headerLine == b.headerLine;
		}),
		candidates.end()
	);
	return candidates;
}


std::vector<FoldRange> EditorCore::EnclosingScopesLocked(size_t line, size_t maxCount)
{
	std::vector<FoldRange> scopes;
	if (maxCount == 0 || line > fModel->Text().NewlineCount()) {
		return scopes;
	}
	for (const FoldRange &candidate : FoldCandidatesLocked(line, line, false)) {
		if (candidate.headerLine < line && candidate.lastLine >= line) {
			scopes.push_back(candidate);
		}
	}
	if (scopes.size() > maxCount) {
		scopes.resize(maxCount);
	}
	return scopes;
}


std::vector<FoldRange> EditorCore::EnclosingScopes(size_t line, size_t maxCount)
{
	ModelLock lock(*fModel);
	Prepare();
	return EnclosingScopesLocked(line, maxCount);
}


// The sticky line of a scope: its header line, or the line before a header
// holding only the opening bracket (a function or block with the bracket on
// its own line).
size_t EditorCore::StickyHeaderLine(size_t headerLine) const
{
	if (headerLine == 0) {
		return headerLine;
	}
	const Rope &text = fModel->Text();
	const size_t start = text.LineStart(headerLine);
	const std::string line = text.Substring(start, std::min<size_t>(text.LineEnd(headerLine) - start, 64));
	const size_t first = line.find_first_not_of(" \t");
	const size_t last = line.find_last_not_of(" \t");
	return first != std::string::npos && first == last && line[first] == '{' ? headerLine - 1 : headerLine;
}


// Outer scopes come first, each taking the next row at the top of the view:
// a scope sticks when its header is above its row and its lines reach that
// row. headerLine of each is the line to show (StickyHeaderLine()).
std::vector<FoldRange> EditorCore::StickyScopes(size_t topRow, size_t maxCount)
{
	ModelLock lock(*fModel);
	Prepare();
	std::vector<FoldRange> scopes;
	const size_t rowCount = fDisplayMap.RowCount();
	if (maxCount == 0 || topRow >= rowCount) {
		return scopes;
	}
	const size_t firstLine = RowAtLocked(topRow).line;
	const size_t lastLine = RowAtLocked(std::min(rowCount - 1, topRow + maxCount)).line;
	for (const FoldRange &candidate : FoldCandidatesLocked(firstLine, lastLine, false)) {
		if (scopes.size() == maxCount) {
			break;
		}
		const size_t slot = topRow + scopes.size();
		const size_t headerRow = fDisplayMap.FirstRowOfLine(candidate.headerLine);
		const size_t endRow = fDisplayMap.FirstRowOfLine(candidate.lastLine + 1);
		if (headerRow < slot && endRow > slot) {
			FoldRange scope = candidate;
			scope.headerLine = StickyHeaderLine(candidate.headerLine);
			scopes.push_back(scope);
		}
	}
	return scopes;
}


void EditorCore::AddSyntaxFoldCandidates(const SyntaxState *syntax, size_t firstLine, size_t lastLine,
	bool headersInRange, std::vector<FoldRange> &candidates)
{
	const Rope &text = fModel->Text();
	TSQueryCursor *cursor = fQueryCursor.Get();
	ts_query_cursor_set_byte_range(
		cursor,
		static_cast<uint32_t>(text.LineStart(firstLine)),
		static_cast<uint32_t>(text.LineEnd(lastLine))
	);
	const TSQuery *query = syntax->GetLanguage().Folds();
	ts_query_cursor_exec(cursor, query, ts_tree_root_node(syntax->Tree()));
	TSQueryMatch match;
	while (ts_query_cursor_next_match(cursor, &match)) {
		// "fold" is the folded node; "fold.end" its closing part, such as
		// "endif()", which stays visible when it starts the last line.
		std::optional<TSNode> folded;
		std::optional<TSNode> closing;
		for (uint16_t i = 0; i < match.capture_count; i++) {
			uint32_t length = 0;
			const char *name = ts_query_capture_name_for_id(query, match.captures[i].index, &length);
			const std::string_view captureName(name, length);
			if (captureName == "fold") {
				folded = match.captures[i].node;
			} else if (captureName == "fold.end") {
				closing = match.captures[i].node;
			}
		}
		if (!folded.has_value()) {
			continue;
		}
		const TSNode node = *folded;
		const size_t start = ts_node_start_byte(node);
		const size_t end = ts_node_end_byte(node);
		if (end <= start) {
			continue;
		}
		const size_t header = text.LineOfOffset(start);
		if (headersInRange && (header < firstLine || header > lastLine)) {
			continue;
		}
		const size_t endLine = text.LineOfOffset(end - 1);
		size_t last = endLine;
		// A closing part starting the last line stays visible: the
		// "fold.end" node, otherwise a closing keyword or bracket: "}",
		// "fi", also "END Name".
		const size_t lineStart = FirstNonBlank(endLine);
		if (closing.has_value()) {
			if (ts_node_start_byte(*closing) == lineStart) {
				last = endLine - 1;
			}
		} else {
			for (uint32_t i = ts_node_child_count(node); i > 0; i--) {
				const TSNode child = ts_node_child(node, i - 1);
				const size_t childStart = ts_node_start_byte(child);
				if (childStart < lineStart) {
					break;
				}
				if (childStart == lineStart) {
					if (!ts_node_is_named(child)) {
						last = endLine - 1;
					}
					break;
				}
			}
		}
		if (last > header) {
			candidates.push_back({header, last});
		}
	}
}


std::vector<FoldRange> EditorCore::FoldCandidates(size_t firstLine, size_t lastLine)
{
	ModelLock lock(*fModel);
	Prepare();
	return FoldCandidatesLocked(firstLine, lastLine, true);
}


std::vector<FoldMarker> EditorCore::FoldMarkers(size_t firstLine, size_t lastLine)
{
	ModelLock lock(*fModel);
	Prepare();
	std::vector<FoldMarker> markers;
	for (const FoldRange &candidate : FoldCandidatesLocked(firstLine, lastLine, true)) {
		markers.push_back({candidate.headerLine, false});
	}
	for (const FoldRange &fold : fFolds) {
		if (fold.headerLine < firstLine || fold.headerLine > lastLine) {
			continue;
		}
		const auto it = std::find_if(markers.begin(), markers.end(), [&fold](const FoldMarker &marker) {
			return marker.line == fold.headerLine;
		});
		if (it != markers.end()) {
			it->folded = true;
		} else {
			markers.push_back({fold.headerLine, true});
		}
	}
	std::sort(markers.begin(), markers.end(), [](const FoldMarker &a, const FoldMarker &b) {
		return a.line < b.line;
	});
	return markers;
}


std::vector<FoldRange> EditorCore::Folds()
{
	ModelLock lock(*fModel);
	Prepare();
	return fFolds;
}


bool EditorCore::IsFolded(size_t headerLine)
{
	ModelLock lock(*fModel);
	Prepare();
	return IsFoldedLocked(headerLine);
}


void EditorCore::Fold(const FoldRange &fold)
{
	ModelLock lock(*fModel);
	Prepare();
	AddFold(fold);
}


void EditorCore::Unfold(size_t headerLine)
{
	ModelLock lock(*fModel);
	Prepare();
	const size_t count = std::erase_if(fFolds, [headerLine](const FoldRange &fold) {
		return fold.headerLine == headerLine;
	});
	if (count > 0) {
		ApplyFolds();
	}
}


void EditorCore::ToggleFoldAtLine(size_t line)
{
	ModelLock lock(*fModel);
	Prepare();
	if (IsFoldedLocked(line)) {
		std::erase_if(fFolds, [line](const FoldRange &fold) {
			return fold.headerLine == line;
		});
		ApplyFolds();
		return;
	}
	const std::vector<FoldRange> candidates = FoldCandidatesLocked(line, line, true);
	if (!candidates.empty()) {
		AddFold(candidates.front());
	}
}


void EditorCore::ToggleFold()
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t start = std::min(fSelection.anchor, fSelection.caret);
	const size_t end = std::max(fSelection.anchor, fSelection.caret);
	const size_t startLine = text.LineOfOffset(start);
	size_t endLine = text.LineOfOffset(end);
	if (endLine > startLine && end == text.LineStart(endLine)) {
		endLine--;
	}
	if (endLine > startLine) {
		AddFold({startLine, endLine});
		return;
	}

	if (IsFoldedLocked(startLine)) {
		std::erase_if(fFolds, [startLine](const FoldRange &fold) {
			return fold.headerLine == startLine;
		});
		ApplyFolds();
		return;
	}
	// The candidate starting on the caret line, else the innermost one
	// containing it.
	std::optional<FoldRange> target;
	for (const FoldRange &candidate : FoldCandidatesLocked(startLine, startLine, false)) {
		if (candidate.headerLine == startLine) {
			target = candidate;
			break;
		}
		if (candidate.headerLine < startLine && startLine <= candidate.lastLine
			&& (!target.has_value() || candidate.headerLine > target->headerLine)) {
			target = candidate;
		}
	}
	if (target.has_value()) {
		AddFold(*target);
	}
}


void EditorCore::FoldAll()
{
	ModelLock lock(*fModel);
	Prepare();
	const std::vector<FoldRange> candidates = FoldCandidatesLocked(0, fModel->Text().NewlineCount(), true);
	if (candidates.empty()) {
		return;
	}
	fFolds = candidates;
	ApplyFolds();
	const Rope &text = fModel->Text();
	if (const std::optional<FoldRange> hiding = HidingFold(text.LineOfOffset(fSelection.caret))) {
		const size_t headerEnd = text.LineEnd(hiding->headerLine);
		SetSelectionLocked({headerEnd, headerEnd});
	}
}


void EditorCore::UnfoldAll()
{
	ModelLock lock(*fModel);
	Prepare();
	if (fFolds.empty()) {
		return;
	}
	fFolds.clear();
	ApplyFolds();
}


void EditorCore::SetHostFolds(const std::vector<HostFold> &folds, bool foldAtCaret)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	const size_t anchorLine = text.LineOfOffset(fSelection.anchor);
	const size_t caretLine = text.LineOfOffset(fSelection.caret);

	fHostRegions.clear();
	std::vector<FoldRange> result;
	for (const FoldRange &fold : fFolds) {
		if (fold.source == FoldSource::User) {
			result.push_back(fold);
		}
	}
	const size_t userCount = result.size();
	for (const HostFold &hostFold : folds) {
		const FoldRange fold {hostFold.headerLine, hostFold.lastLine, FoldSource::Host};
		if (fold.lastLine <= fold.headerLine || fold.lastLine > text.NewlineCount()) {
			continue;
		}
		fHostRegions.push_back(fold);
		if (!hostFold.folded) {
			continue;
		}
		auto hides = [&fold](size_t line) {
			return fold.headerLine < line && line <= fold.lastLine;
		};
		if (!foldAtCaret && (hides(anchorLine) || hides(caretLine))) {
			continue;
		}
		const bool sameHeader = std::any_of(result.begin(), result.end(), [&fold](const FoldRange &other) {
			return other.headerLine == fold.headerLine;
		});
		if (!sameHeader) {
			result.push_back(fold);
		}
	}
	auto byHeader = [](const FoldRange &a, const FoldRange &b) {
		return a.headerLine < b.headerLine;
	};
	std::stable_sort(fHostRegions.begin(), fHostRegions.end(), byHeader);
	if (result.size() > userCount) {
		std::stable_sort(result.begin(), result.end(), byHeader);
	}
	// The markers of regions may have changed even if the folds did not.
	fDirty.marksChanged = true;
	if (result == fFolds) {
		return;
	}
	fFolds = std::move(result);
	ApplyFolds();

	// A hidden caret moves to the end of the header line.
	if (const std::optional<FoldRange> hiding = HidingFold(caretLine)) {
		const size_t headerEnd = text.LineEnd(hiding->headerLine);
		fGoalColumn.reset();
		SetSelectionLocked({headerEnd, headerEnd});
	} else if (HidingFold(anchorLine).has_value()) {
		SetSelectionLocked({fSelection.caret, fSelection.caret});
	}
}

//#pragma mark - Decorations

void EditorCore::SetDecorations(std::vector<Decoration> decorations)
{
	ModelLock lock(*fModel);
	Prepare();
	NormalizeDecorations(decorations, fModel->Text().Length());
	if (decorations == fDecorations) {
		return;
	}
	fDecorations = std::move(decorations);
	fDirty.AddAll();
}


std::vector<Decoration> EditorCore::Decorations()
{
	ModelLock lock(*fModel);
	Prepare();
	return fDecorations;
}


void EditorCore::MapDecorations(const EditRecord &edit)
{
	for (Decoration &decoration : fDecorations) {
		MapDecoration(decoration, edit);
	}
}


std::vector<uint64_t> EditorCore::MarkRevisions() const
{
	std::vector<uint64_t> revisions {fModel->SharedMarks().Revision()};
	for (const BReference<MarkSet> &set : fMarkSets) {
		revisions.push_back(set->Revision());
	}
	return revisions;
}


std::vector<uint64_t> EditorCore::DecorationRevisions() const
{
	std::vector<uint64_t> revisions;
	for (const BReference<DecorationSet> &set : fDecorationSets) {
		revisions.push_back(set->Revision());
	}
	return revisions;
}


// Repaints if a shown set changed; changes of other sets of the model are
// ignored.
void EditorCore::CheckOverlays()
{
	std::vector<uint64_t> markRevisions = MarkRevisions();
	if (markRevisions != fMarkRevisions) {
		fMarkRevisions = std::move(markRevisions);
		// Marks are drawn in the gutter and may color line backgrounds.
		fDirty.marksChanged = true;
		fDirty.AddAll();
	}
	std::vector<uint64_t> decorationRevisions = DecorationRevisions();
	if (decorationRevisions != fDecorationRevisions) {
		fDecorationRevisions = std::move(decorationRevisions);
		fDirty.AddAll();
	}
}


void EditorCore::SetMarkSets(std::vector<BReference<MarkSet>> sets)
{
	ModelLock lock(*fModel);
	Prepare();
	for (const BReference<MarkSet> &set : sets) {
		if (set.Get() == nullptr || &set->Model() != fModel.Get()) {
			throw std::invalid_argument("EditorCore::SetMarkSets: set of another model");
		}
	}
	fMarkSets = std::move(sets);
	fMarkRevisions = MarkRevisions();
	fDirty.marksChanged = true;
	fDirty.AddAll();
}


void EditorCore::SetDecorationSets(std::vector<BReference<DecorationSet>> sets)
{
	ModelLock lock(*fModel);
	Prepare();
	for (const BReference<DecorationSet> &set : sets) {
		if (set.Get() == nullptr || &set->Model() != fModel.Get()) {
			throw std::invalid_argument("EditorCore::SetDecorationSets: set of another model");
		}
	}
	fDecorationSets = std::move(sets);
	fDecorationRevisions = DecorationRevisions();
	fDirty.AddAll();
}


LineDecorations EditorCore::DecorationsOfLines(size_t firstLine, size_t lastLine)
{
	ModelLock lock(*fModel);
	Prepare();
	const Rope &text = fModel->Text();
	std::vector<const Decoration*> decorations;
	for (const BReference<DecorationSet> &set : fDecorationSets) {
		for (const Decoration &decoration : set->Decorations()) {
			decorations.push_back(&decoration);
		}
	}
	for (const Decoration &decoration : fDecorations) {
		decorations.push_back(&decoration);
	}

	LineDecorations result;
	for (const Decoration *item : decorations) {
		const Decoration &decoration = *item;
		const size_t startLine = text.LineOfOffset(decoration.start);
		switch (decoration.kind) {
			case DecorationKind::LineBackground: {
				const size_t endLine = decoration.end > decoration.start
					? text.LineOfOffset(decoration.end - 1)
					: startLine;
				for (size_t line = std::max(startLine, firstLine); line <= std::min(endLine, lastLine); line++) {
					result.lineBackgrounds.emplace_back(line, decoration.color);
				}
				break;
			}
			case DecorationKind::RangeBackground:
				if (decoration.end > decoration.start && startLine <= lastLine
					&& text.LineOfOffset(decoration.end) >= firstLine) {
					result.ranges.push_back(decoration);
				}
				break;
			case DecorationKind::GapLine:
				if (startLine >= firstLine && startLine <= lastLine) {
					const bool bottom = decoration.start != text.LineStart(startLine);
					result.gaps.push_back({startLine, bottom, decoration.color});
				}
				break;
		}
	}
	return result;
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
	if (IsReadOnly()) {
		return false;
	}
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
	if (IsReadOnly()) {
		return false;
	}
	const std::optional<UndoResult> result = fModel->Redo();
	if (!result.has_value()) {
		return false;
	}
	ApplyUndoResult(result, fModel->Sync(fObserver));
	return true;
}


DirtyRows EditorCore::TakeDirty()
{
	if (fMaxColumnsGrew) {
		fMaxColumnsGrew = false;
		fDirty.scrollRangeChanged = true;
	}
	const DirtyRows dirty = fDirty;
	fDirty = {};
	return dirty;
}


} // CodeEdit
