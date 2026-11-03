#include <CodeEditView/TextModel.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <system_error>

#include <DataIO.h>
#include <Message.h>

#include "UndoStack.h"
#include "support/Assert.h"


namespace CodeEdit {


static constexpr size_t kMaxLogSize = 4096;
static constexpr size_t kIOBufferSize = 64 * 1024;
static constexpr std::string_view kUtf8Bom = "\xef\xbb\xbf";


static void CheckThrow(status_t res)
{
	if (res < B_OK) {
		throw std::system_error(res, std::generic_category());
	}
}


static TextPoint Advance(const TextPoint &start, std::string_view text)
{
	const size_t lastNewline = text.rfind('\n');
	if (lastNewline == std::string_view::npos) {
		return {start.line, start.column + text.size()};
	}
	const size_t newlines = std::count(text.begin(), text.end(), '\n');
	return {start.line + newlines, text.size() - lastNewline - 1};
}


// Converts CRLF and lone CR to LF and reports which line ending dominated.
static std::string NormalizeLineEndings(std::string_view data, LineEnding &dominant)
{
	size_t lfCount = 0;
	size_t crlfCount = 0;
	size_t crCount = 0;
	std::string text;
	text.reserve(data.size());
	for (size_t i = 0; i < data.size(); i++) {
		const char ch = data[i];
		if (ch == '\r') {
			if (i + 1 < data.size() && data[i + 1] == '\n') {
				crlfCount++;
				i++;
			} else {
				crCount++;
			}
			text += '\n';
			continue;
		}
		if (ch == '\n') {
			lfCount++;
		}
		text += ch;
	}

	dominant = LineEnding::LF;
	if (crlfCount > lfCount && crlfCount >= crCount) {
		dominant = LineEnding::CRLF;
	} else if (crCount > lfCount && crCount > crlfCount) {
		dominant = LineEnding::CR;
	}
	return text;
}


//#pragma mark - TextModel

TextModel::TextModel():
	fLock("TextModel"),
	fUndo(std::make_unique<UndoStack>())
{
}


TextModel::~TextModel() = default;


void TextModel::AssertLocked() const
{
	Assert(fLock.IsLocked(), "TextModel is not locked by the calling thread");
}


void TextModel::CheckRange(size_t offset, size_t length) const
{
	if (offset > fText.Length() || length > fText.Length() - offset) {
		throw std::out_of_range("TextModel: range out of range");
	}
}


bool TextModel::Lock()
{
	return fLock.Lock();
}


void TextModel::Unlock()
{
	std::vector<Notification> notifications;
	if (fLock.CountLocks() == 1) {
		Assert(fGroupDepth == 0, "TextModel unlocked with an open edit group");
		notifications = CollectNotifications();
	}
	fLock.Unlock();

	std::vector<ObserverId> deadObservers;
	for (const auto &[id, messenger] : notifications) {
		BMessage message(kMsgModelChanged);
		message.AddPointer("model", this);
		if (messenger.SendMessage(&message) == B_BAD_PORT_ID) {
			deadObservers.push_back(id);
		}
	}
	if (deadObservers.empty()) {
		return;
	}

	ModelLock lock(*this);
	for (const ObserverId id : deadObservers) {
		fObservers.erase(id);
	}
	TrimLog();
}


bool TextModel::IsLocked() const
{
	return fLock.IsLocked();
}


const Rope &TextModel::Text() const
{
	AssertLocked();
	return fText;
}


uint64_t TextModel::Revision() const
{
	AssertLocked();
	return fRevision;
}


//#pragma mark - Editing

EditRecord TextModel::ApplyTextEdit(size_t offset, size_t length, std::string_view text, std::string &removed)
{
	removed = fText.Substring(offset, length);

	EditRecord edit;
	edit.offset = offset;
	edit.oldLength = length;
	edit.newLength = text.size();
	edit.start.line = fText.LineOfOffset(offset);
	edit.start.column = offset - fText.LineStart(edit.start.line);
	edit.oldEnd = Advance(edit.start, removed);
	edit.newEnd = Advance(edit.start, text);

	fText.Replace(offset, length, text);
	edit.revision = ++fRevision;
	fLog.push_back(edit);
	TrimLog();
	return edit;
}


EditRecord TextModel::ApplyEdit(size_t offset, size_t length, std::string_view text, std::string &removed)
{
	const std::vector<MarkId> deletedLineMarks = WholeLineDeletedMarks(offset, length);
	const EditRecord edit = ApplyTextEdit(offset, length, text, removed);
	UpdateMarks(edit, deletedLineMarks);
	return edit;
}


void TextModel::BeginGroup(ObserverId owner, EditKind kind, const Selection &before)
{
	AssertLocked();
	if (fGroupDepth++ > 0) {
		return;
	}
	fGroupOwner = owner;
	fGroupKind = kind;
	fGroupBefore = before;
	fGroupHasSteps = false;
}


void TextModel::EndGroup(const Selection &after)
{
	AssertLocked();
	Assert(fGroupDepth > 0, "TextModel::EndGroup without BeginGroup");
	fGroupDepth--;
	if (fGroupDepth == 0 && fGroupHasSteps) {
		fUndo->EndGroup(after);
	}
}


void TextModel::Replace(size_t offset, size_t length, std::string_view text)
{
	AssertLocked();
	CheckRange(offset, length);
	if (length == 0 && text.empty()) {
		return;
	}

	const bool implicitGroup = fGroupDepth == 0;
	if (implicitGroup) {
		BeginGroup(kNoObserver, EditKind::Other, {offset, offset + length});
	}

	UndoStack::Step step;
	step.offset = offset;
	step.inserted = text;
	const EditRecord edit = ApplyEdit(offset, length, step.inserted, step.removed);

	if (!fGroupHasSteps) {
		fUndo->BeginGroup(fGroupOwner, fGroupKind, fGroupBefore, std::move(step));
		fGroupHasSteps = true;
	} else {
		fUndo->Append(std::move(step));
	}

	if (implicitGroup) {
		EndGroup({edit.NewEnd(), edit.NewEnd()});
	}
}


void TextModel::SetText(std::string_view text)
{
	AssertLocked();
	ResetText(text);
}


// Replaces the whole text without undo; marks keep their line numbers.
void TextModel::ResetText(std::string_view text)
{
	if (fGroupDepth > 0) {
		throw std::logic_error("TextModel: text reset inside an edit group");
	}

	std::vector<std::pair<Mark*, size_t>> markLines;
	for (auto &[id, mark] : fMarks) {
		markLines.emplace_back(&mark, fText.LineOfOffset(mark.anchor));
	}

	const std::string newText(text);
	std::string removed;
	ApplyTextEdit(0, fText.Length(), newText, removed);

	for (const auto &[mark, line] : markLines) {
		mark->anchor = fText.LineStart(std::min(line, fText.NewlineCount()));
	}
	fUndo->Clear();
}


//#pragma mark - Undo

bool TextModel::CanUndo() const
{
	AssertLocked();
	return fUndo->CanUndo();
}


bool TextModel::CanRedo() const
{
	AssertLocked();
	return fUndo->CanRedo();
}


std::optional<UndoResult> TextModel::Undo()
{
	AssertLocked();
	if (fGroupDepth > 0) {
		throw std::logic_error("TextModel::Undo: edit group is open");
	}
	if (!fUndo->CanUndo()) {
		return std::nullopt;
	}

	UndoStack::Group group = fUndo->PopUndo();
	std::string removed;
	for (auto step = group.steps.rbegin(); step != group.steps.rend(); ++step) {
		ApplyEdit(step->offset, step->inserted.size(), step->removed, removed);
	}
	const UndoResult result {group.owner, group.before};
	fUndo->PushRedo(std::move(group));
	return result;
}


std::optional<UndoResult> TextModel::Redo()
{
	AssertLocked();
	if (fGroupDepth > 0) {
		throw std::logic_error("TextModel::Redo: edit group is open");
	}
	if (!fUndo->CanRedo()) {
		return std::nullopt;
	}

	UndoStack::Group group = fUndo->PopRedo();
	std::string removed;
	for (const UndoStack::Step &step : group.steps) {
		ApplyEdit(step.offset, step.removed.size(), step.inserted, removed);
	}
	const UndoResult result {group.owner, group.after};
	fUndo->PushUndo(std::move(group));
	return result;
}


void TextModel::BreakUndoCoalescing()
{
	AssertLocked();
	fUndo->BreakCoalescing();
}


bool TextModel::IsModified() const
{
	AssertLocked();
	return fUndo->IsModified();
}


void TextModel::SetSavePoint()
{
	AssertLocked();
	fUndo->SetSavePoint();
}


//#pragma mark - Observers

ObserverId TextModel::AddObserver(const BMessenger &messenger)
{
	AssertLocked();
	const ObserverId id = fNextObserverId++;
	Observer &observer = fObservers[id];
	observer.messenger = messenger;
	observer.revision = fRevision;
	observer.marksRevision = fMarksRevision;
	return id;
}


void TextModel::RemoveObserver(ObserverId id)
{
	AssertLocked();
	fObservers.erase(id);
	TrimLog();
}


int32_t TextModel::CountObservers() const
{
	AssertLocked();
	return static_cast<int32_t>(fObservers.size());
}


ModelChanges TextModel::Sync(ObserverId id)
{
	AssertLocked();
	const auto it = fObservers.find(id);
	if (it == fObservers.end()) {
		throw std::invalid_argument("TextModel::Sync: unknown observer");
	}
	Observer &observer = it->second;

	ModelChanges changes;
	changes.revision = fRevision;
	changes.marksChanged = observer.marksRevision != fMarksRevision;
	if (observer.revision != fRevision) {
		if (fLog.empty() || fLog.front().revision > observer.revision + 1) {
			changes.reset = true;
		} else {
			for (const EditRecord &edit : fLog) {
				if (edit.revision > observer.revision) {
					changes.edits.push_back(edit);
				}
			}
		}
	}

	observer.revision = fRevision;
	observer.marksRevision = fMarksRevision;
	observer.pending = false;
	TrimLog();
	return changes;
}


// Drops records every observer has seen, and bounds the log size; observers
// that fall behind the bound get a reset from Sync().
void TextModel::TrimLog()
{
	uint64_t minRevision = fRevision;
	for (const auto &[id, observer] : fObservers) {
		minRevision = std::min(minRevision, observer.revision);
	}
	while (!fLog.empty() && (fLog.front().revision <= minRevision || fLog.size() > kMaxLogSize)) {
		fLog.pop_front();
	}
}


std::vector<TextModel::Notification> TextModel::CollectNotifications()
{
	std::vector<Notification> notifications;
	for (auto &[id, observer] : fObservers) {
		if (observer.pending) {
			continue;
		}
		if (observer.revision == fRevision && observer.marksRevision == fMarksRevision) {
			continue;
		}
		observer.pending = true;
		notifications.emplace_back(id, observer.messenger);
	}
	return notifications;
}


//#pragma mark - Marks

// Marks whose line, including its newline, lies completely inside the range
// about to be removed.
std::vector<MarkId> TextModel::WholeLineDeletedMarks(size_t offset, size_t length) const
{
	std::vector<MarkId> result;
	if (length == 0) {
		return result;
	}
	for (const auto &[id, mark] : fMarks) {
		if (mark.anchor < offset || mark.anchor >= offset + length) {
			continue;
		}
		const size_t line = fText.LineOfOffset(mark.anchor);
		const size_t lineEnd = line < fText.NewlineCount() ? fText.LineStart(line + 1) : fText.Length();
		if (lineEnd > mark.anchor && offset + length >= lineEnd) {
			result.push_back(id);
		}
	}
	return result;
}


static bool IsLineStart(const Rope &text, size_t offset)
{
	return offset == 0 || text.ByteAt(offset - 1) == '\n';
}


void TextModel::UpdateMarks(const EditRecord &edit, const std::vector<MarkId> &deletedLineMarks)
{
	bool removed = false;
	for (auto it = fMarks.begin(); it != fMarks.end();) {
		Mark &mark = it->second;
		const size_t mapped = edit.MapOffset(mark.anchor, Gravity::Right);

		bool lineDeleted = std::find(deletedLineMarks.begin(), deletedLineMarks.end(), it->first)
			!= deletedLineMarks.end();
		if (!lineDeleted && edit.oldLength > 0 && mark.anchor > edit.offset && mark.anchor <= edit.OldEnd()) {
			// The newline before the mark was deleted: joined into another line.
			lineDeleted = !IsLineStart(fText, mapped);
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
		mark.anchor = fText.LineStart(fText.LineOfOffset(mark.anchor));
		++it;
	}
	if (removed) {
		fMarksRevision++;
	}
}


MarkId TextModel::AddMark(uint32_t kind, size_t line, int64_t data, MarkDeletePolicy policy)
{
	AssertLocked();
	const size_t anchor = fText.LineStart(line);
	const MarkId id = fNextMarkId++;
	fMarks[id] = {kind, anchor, data, policy};
	fMarksRevision++;
	return id;
}


bool TextModel::RemoveMark(MarkId id)
{
	AssertLocked();
	if (fMarks.erase(id) == 0) {
		return false;
	}
	fMarksRevision++;
	return true;
}


void TextModel::RemoveMarks(uint32_t kind)
{
	AssertLocked();
	const size_t count = std::erase_if(fMarks, [kind](const auto &item) {
		return item.second.kind == kind;
	});
	if (count > 0) {
		fMarksRevision++;
	}
}


bool TextModel::MoveMark(MarkId id, size_t line)
{
	AssertLocked();
	const auto it = fMarks.find(id);
	if (it == fMarks.end()) {
		return false;
	}
	it->second.anchor = fText.LineStart(line);
	fMarksRevision++;
	return true;
}


std::optional<MarkInfo> TextModel::FindMark(MarkId id) const
{
	AssertLocked();
	const auto it = fMarks.find(id);
	if (it == fMarks.end()) {
		return std::nullopt;
	}
	const Mark &mark = it->second;
	return MarkInfo {id, mark.kind, fText.LineOfOffset(mark.anchor), mark.data};
}


std::vector<MarkInfo> TextModel::Marks(size_t firstLine, size_t lastLine) const
{
	AssertLocked();
	std::vector<MarkInfo> result;
	for (const auto &[id, mark] : fMarks) {
		const size_t line = fText.LineOfOffset(mark.anchor);
		if (line >= firstLine && line <= lastLine) {
			result.push_back({id, mark.kind, line, mark.data});
		}
	}
	std::sort(result.begin(), result.end(), [](const MarkInfo &a, const MarkInfo &b) {
		return a.line != b.line ? a.line < b.line : a.id < b.id;
	});
	return result;
}


//#pragma mark - File I/O

void TextModel::Load(BPositionIO &stream)
{
	AssertLocked();
	std::string data;
	std::string buffer(kIOBufferSize, '\0');
	for (;;) {
		const ssize_t size = stream.Read(buffer.data(), buffer.size());
		if (size < 0) {
			CheckThrow(static_cast<status_t>(size));
		}
		if (size == 0) {
			break;
		}
		data.append(buffer.data(), size);
	}

	const bool hasBom = data.starts_with(kUtf8Bom);
	LineEnding lineEnding;
	const std::string text = NormalizeLineEndings(std::string_view(data).substr(hasBom ? kUtf8Bom.size() : 0), lineEnding);
	ResetText(text);
	fLineEnding = lineEnding;
	fHasBom = hasBom;
}


// Writes at the stream's current position; the caller truncates if needed.
void TextModel::Save(BPositionIO &stream) const
{
	AssertLocked();
	std::string_view newline = "\n";
	switch (fLineEnding) {
		case LineEnding::CRLF:
			newline = "\r\n";
			break;
		case LineEnding::CR:
			newline = "\r";
			break;
		default:
			break;
	}

	std::string buffer;
	buffer.reserve(2 * kIOBufferSize);
	if (fHasBom) {
		buffer += kUtf8Bom;
	}
	size_t pos = 0;
	while (pos < fText.Length()) {
		const std::string_view chunk = fText.ChunkAt(pos);
		for (const char ch : chunk) {
			if (ch == '\n') {
				buffer += newline;
			} else {
				buffer += ch;
			}
		}
		pos += chunk.size();
		if (buffer.size() >= kIOBufferSize) {
			CheckThrow(stream.WriteExactly(buffer.data(), buffer.size()));
			buffer.clear();
		}
	}
	if (!buffer.empty()) {
		CheckThrow(stream.WriteExactly(buffer.data(), buffer.size()));
	}
}


LineEnding TextModel::GetLineEnding() const
{
	AssertLocked();
	return fLineEnding;
}


void TextModel::SetLineEnding(LineEnding lineEnding)
{
	AssertLocked();
	fLineEnding = lineEnding;
}


bool TextModel::HasBom() const
{
	AssertLocked();
	return fHasBom;
}


void TextModel::SetHasBom(bool hasBom)
{
	AssertLocked();
	fHasBom = hasBom;
}


} // CodeEdit
