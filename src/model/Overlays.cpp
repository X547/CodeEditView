#include <CodeEditView/Overlays.h>

#include <algorithm>

#include <CodeEditView/TextModel.h>

#include "DecorationMapping.h"


namespace CodeEdit {


static bool IsLineStart(const Rope &text, size_t offset)
{
	return offset == 0 || text.ByteAt(offset - 1) == '\n';
}


static TextPoint PointOfOffset(const Rope &text, size_t offset)
{
	const size_t line = text.LineOfOffset(offset);
	return {line, offset - text.LineStart(line)};
}


// The offset of a point, clamped to its line and the text.
static size_t OffsetOfPoint(const Rope &text, const TextPoint &point)
{
	const size_t line = std::min(point.line, text.NewlineCount());
	const size_t start = text.LineStart(line);
	const size_t end = line < text.NewlineCount() ? text.LineStart(line + 1) - 1 : text.Length();
	return start + std::min(point.column, end - start);
}


void NormalizeDecorations(std::vector<Decoration> &decorations, size_t length)
{
	for (Decoration &decoration : decorations) {
		decoration.start = std::min(decoration.start, length);
		decoration.end = decoration.kind == DecorationKind::GapLine
			? decoration.start
			: std::clamp(decoration.end, decoration.start, length);
	}
	std::stable_sort(decorations.begin(), decorations.end(), [](const Decoration &a, const Decoration &b) {
		return a.start < b.start;
	});
}


void MapDecoration(Decoration &decoration, const EditRecord &edit)
{
	if (decoration.kind == DecorationKind::GapLine) {
		decoration.start = decoration.end = edit.MapOffset(decoration.start, Gravity::Left);
		return;
	}
	const size_t start = edit.MapOffset(decoration.start, Gravity::Right);
	const size_t end = edit.MapOffset(decoration.end, Gravity::Left);
	decoration.start = start;
	decoration.end = std::max(start, end);
}


//#pragma mark - MarkSet

MarkSet::MarkSet(TextModel &model, bool ownedByModel):
	fModel(&model),
	fOwnedByModel(ownedByModel)
{
	// The model is being constructed when it creates its shared set.
	if (!ownedByModel) {
		fModel->AcquireReference();
		fModel->Lock();
	}
	fModel->fMarkSets.push_back(this);
	if (!ownedByModel) {
		fModel->Unlock();
	}
}


MarkSet::MarkSet(TextModel &model):
	MarkSet(model, false)
{
}


MarkSet::~MarkSet()
{
	{
		ModelLock lock(*fModel);
		std::erase(fModel->fMarkSets, this);
		if (!fMarks.empty()) {
			fModel->OverlaysChanged();
		}
	}
	if (!fOwnedByModel) {
		fModel->ReleaseReference();
	}
}


void MarkSet::Changed()
{
	fRevision++;
	fModel->OverlaysChanged();
}


uint64_t MarkSet::Revision() const
{
	fModel->AssertLocked();
	return fRevision;
}


// Marks whose line, including its newline, lies completely inside the range
// about to be removed.
std::vector<MarkId> MarkSet::WholeLineDeletedMarks(const Rope &text, size_t offset, size_t length) const
{
	std::vector<MarkId> result;
	if (length == 0) {
		return result;
	}
	for (const auto &[id, mark] : fMarks) {
		if (mark.anchor < offset || mark.anchor >= offset + length) {
			continue;
		}
		const size_t line = text.LineOfOffset(mark.anchor);
		const size_t lineEnd = line < text.NewlineCount() ? text.LineStart(line + 1) : text.Length();
		if (lineEnd > mark.anchor && offset + length >= lineEnd) {
			result.push_back(id);
		}
	}
	return result;
}


void MarkSet::ApplyEdit(const Rope &text, const EditRecord &edit, const std::vector<MarkId> &deletedLineMarks)
{
	bool removed = false;
	for (auto it = fMarks.begin(); it != fMarks.end();) {
		Mark &mark = it->second;
		const size_t mapped = edit.MapOffset(mark.anchor, Gravity::Right);

		bool lineDeleted = std::find(deletedLineMarks.begin(), deletedLineMarks.end(), it->first)
			!= deletedLineMarks.end();
		if (!lineDeleted && edit.oldLength > 0 && mark.anchor > edit.offset && mark.anchor <= edit.OldEnd()) {
			// The newline before the mark was deleted: joined into another line.
			lineDeleted = !IsLineStart(text, mapped);
		}

		if (lineDeleted) {
			if (mark.policy == MarkDeletePolicy::Remove) {
				it = fMarks.erase(it);
				removed = true;
				continue;
			}
			mark.anchor = edit.offset;
		} else {
			mark.anchor = mapped;
		}
		mark.anchor = text.LineStart(text.LineOfOffset(mark.anchor));
		++it;
	}
	if (removed) {
		Changed();
	}
}


// Lines of the marks in id order.
std::vector<size_t> MarkSet::Lines(const Rope &text) const
{
	std::vector<size_t> lines;
	lines.reserve(fMarks.size());
	for (const auto &[id, mark] : fMarks) {
		lines.push_back(text.LineOfOffset(mark.anchor));
	}
	return lines;
}


void MarkSet::RestoreLines(const Rope &text, const std::vector<size_t> &lines)
{
	size_t index = 0;
	for (auto &[id, mark] : fMarks) {
		mark.anchor = text.LineStart(std::min(lines[index++], text.NewlineCount()));
	}
}


MarkId MarkSet::AddMark(uint32_t kind, size_t line, int64_t data, MarkDeletePolicy policy)
{
	fModel->AssertLocked();
	const size_t anchor = fModel->Text().LineStart(line);
	const MarkId id = fNextId++;
	fMarks[id] = {kind, anchor, data, policy};
	Changed();
	return id;
}


bool MarkSet::RemoveMark(MarkId id)
{
	fModel->AssertLocked();
	if (fMarks.erase(id) == 0) {
		return false;
	}
	Changed();
	return true;
}


void MarkSet::RemoveMarks(uint32_t kind)
{
	fModel->AssertLocked();
	const size_t count = std::erase_if(fMarks, [kind](const auto &item) {
		return item.second.kind == kind;
	});
	if (count > 0) {
		Changed();
	}
}


void MarkSet::Clear()
{
	fModel->AssertLocked();
	if (fMarks.empty()) {
		return;
	}
	fMarks.clear();
	Changed();
}


bool MarkSet::MoveMark(MarkId id, size_t line)
{
	fModel->AssertLocked();
	const auto it = fMarks.find(id);
	if (it == fMarks.end()) {
		return false;
	}
	it->second.anchor = fModel->Text().LineStart(line);
	Changed();
	return true;
}


bool MarkSet::SetMarkKind(MarkId id, uint32_t kind)
{
	fModel->AssertLocked();
	const auto it = fMarks.find(id);
	if (it == fMarks.end()) {
		return false;
	}
	if (it->second.kind != kind) {
		it->second.kind = kind;
		Changed();
	}
	return true;
}


std::optional<MarkInfo> MarkSet::FindMark(MarkId id) const
{
	fModel->AssertLocked();
	const auto it = fMarks.find(id);
	if (it == fMarks.end()) {
		return std::nullopt;
	}
	const Mark &mark = it->second;
	return MarkInfo {id, mark.kind, fModel->Text().LineOfOffset(mark.anchor), mark.data};
}


std::vector<MarkInfo> MarkSet::Marks(size_t firstLine, size_t lastLine) const
{
	fModel->AssertLocked();
	const Rope &text = fModel->Text();
	std::vector<MarkInfo> result;
	for (const auto &[id, mark] : fMarks) {
		const size_t line = text.LineOfOffset(mark.anchor);
		if (line >= firstLine && line <= lastLine) {
			result.push_back({id, mark.kind, line, mark.data});
		}
	}
	std::sort(result.begin(), result.end(), [](const MarkInfo &a, const MarkInfo &b) {
		return a.line != b.line ? a.line < b.line : a.id < b.id;
	});
	return result;
}


//#pragma mark - DecorationSet

DecorationSet::DecorationSet(TextModel &model):
	fModel(&model)
{
	fModel->AcquireReference();
	ModelLock lock(*fModel);
	fModel->fDecorationSets.push_back(this);
}


DecorationSet::~DecorationSet()
{
	{
		ModelLock lock(*fModel);
		std::erase(fModel->fDecorationSets, this);
		if (!fDecorations.empty()) {
			fModel->OverlaysChanged();
		}
	}
	fModel->ReleaseReference();
}


uint64_t DecorationSet::Revision() const
{
	fModel->AssertLocked();
	return fRevision;
}


void DecorationSet::ApplyEdit(const EditRecord &edit)
{
	for (Decoration &decoration : fDecorations) {
		MapDecoration(decoration, edit);
	}
}


std::vector<std::pair<TextPoint, TextPoint>> DecorationSet::Points(const Rope &text) const
{
	std::vector<std::pair<TextPoint, TextPoint>> points;
	points.reserve(fDecorations.size());
	for (const Decoration &decoration : fDecorations) {
		points.emplace_back(PointOfOffset(text, decoration.start), PointOfOffset(text, decoration.end));
	}
	return points;
}


void DecorationSet::RestorePoints(const Rope &text, const std::vector<std::pair<TextPoint, TextPoint>> &points)
{
	for (size_t i = 0; i < fDecorations.size(); i++) {
		fDecorations[i].start = OffsetOfPoint(text, points[i].first);
		fDecorations[i].end = OffsetOfPoint(text, points[i].second);
	}
	NormalizeDecorations(fDecorations, text.Length());
}


void DecorationSet::SetDecorations(std::vector<Decoration> decorations)
{
	fModel->AssertLocked();
	NormalizeDecorations(decorations, fModel->Text().Length());
	if (decorations == fDecorations) {
		return;
	}
	fDecorations = std::move(decorations);
	fRevision++;
	fModel->OverlaysChanged();
}


const std::vector<Decoration> &DecorationSet::Decorations() const
{
	fModel->AssertLocked();
	return fDecorations;
}


} // CodeEdit
