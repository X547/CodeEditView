#include <CodeEditView/TextModel.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <system_error>

#include <DataIO.h>
#include <Message.h>

#include "UndoStack.h"
#include "support/Assert.h"
#include "syntax/Highlighter.h"
#include "syntax/Language.h"
#include "syntax/Outline.h"
#include "syntax/SyntaxState.h"


namespace CodeEdit {


// Edit records observers have not seen; one that falls further behind gets
// a reset. Large enough for replacing all matches of a search at once.
static constexpr size_t kMaxLogSize = 256 * 1024;
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


static TextPoint EndPoint(const Rope &text)
{
	const size_t line = text.NewlineCount();
	return {line, text.Length() - text.LineStart(line)};
}


//#pragma mark - TextSnapshot

// LF text is passed on as the rope holds it; other line endings go through
// a buffer.
void TextSnapshot::Write(const std::function<void(std::string_view data)> &write) const
{
	if (hasBom) {
		write(kUtf8Bom);
	}
	std::string_view newline = "\n";
	switch (lineEnding) {
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
	if (newline.size() != 1 || newline[0] != '\n') {
		buffer.reserve(kIOBufferSize + kIOBufferSize / 2);
	}
	size_t pos = 0;
	while (pos < text.Length()) {
		const std::string_view chunk = text.ChunkAt(pos);
		pos += chunk.size();
		if (newline == "\n") {
			write(chunk);
			continue;
		}
		size_t start = 0;
		while (start < chunk.size()) {
			const size_t end = std::min(chunk.find('\n', start), chunk.size());
			buffer.append(chunk.substr(start, end - start));
			if (end < chunk.size()) {
				buffer += newline;
			}
			start = end + 1;
		}
		if (buffer.size() >= kIOBufferSize) {
			write(buffer);
			buffer.clear();
		}
	}
	if (!buffer.empty()) {
		write(buffer);
	}
}


//#pragma mark - TextModel

TextModel::TextModel():
	fLock("TextModel"),
	fUndo(std::make_unique<UndoStack>())
{
	fSharedMarks.reset(new MarkSet(*this, true));
}


// Background parsing threads may still be waiting for the lock; they quit
// once they get it.
TextModel::~TextModel()
{
	fLock.Lock();
	fSharedMarks.reset();
	// Other sets hold a reference to the model.
	Assert(fMarkSets.empty() && fDecorationSets.empty(), "TextModel destroyed with attached sets");
	fSyntax.reset();
	const std::vector<thread_id> workers = std::move(fRetiredWorkers);
	fLock.Unlock();

	for (const thread_id thread : workers) {
		status_t result;
		wait_for_thread(thread, &result);
	}
}


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
	if (fSyntax != nullptr) {
		fSyntax->TextEdited(edit);
	}
	fLog.push_back(edit);
	TrimLog();
	return edit;
}


EditRecord TextModel::ApplyEdit(size_t offset, size_t length, std::string_view text, std::string &removed)
{
	// Whole deleted lines are known only before the edit.
	std::vector<std::vector<MarkId>> deletedLineMarks;
	deletedLineMarks.reserve(fMarkSets.size());
	for (const MarkSet *set : fMarkSets) {
		deletedLineMarks.push_back(set->WholeLineDeletedMarks(fText, offset, length));
	}

	const EditRecord edit = ApplyTextEdit(offset, length, text, removed);

	for (size_t i = 0; i < fMarkSets.size(); i++) {
		fMarkSets[i]->ApplyEdit(fText, edit, deletedLineMarks[i]);
	}
	for (DecorationSet *set : fDecorationSets) {
		set->ApplyEdit(edit);
	}
	return edit;
}


Selection MapSelection(const Selection &selection, const EditRecord &edit)
{
	const bool forward = selection.anchor <= selection.caret;
	size_t start = std::min(selection.anchor, selection.caret);
	size_t end = std::max(selection.anchor, selection.caret);
	if (start == end) {
		start = end = edit.MapOffset(start, Gravity::Right);
	} else {
		start = edit.MapOffset(start, Gravity::Right);
		end = edit.MapOffset(end, Gravity::Left);
		if (start > end) {
			start = end;
		}
	}
	return forward ? Selection {start, end} : Selection {end, start};
}


void TextModel::BeginGroup(std::string_view name, ObserverId owner, EditKind kind, const Selection &before)
{
	AssertLocked();
	if (fGroupDepth++ > 0) {
		return;
	}
	fGroupOwner = owner;
	fGroupKind = kind;
	fGroupName = name;
	fGroupBefore = before;
	fGroupAfter = before;
	fGroupHasSteps = false;
}


void TextModel::EndGroup()
{
	AssertLocked();
	Assert(fGroupDepth > 0, "TextModel::EndGroup without BeginGroup");
	fGroupDepth--;
	if (fGroupDepth == 0 && fGroupHasSteps) {
		fUndo->EndGroup(fGroupAfter);
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
		BeginGroup({}, kNoObserver, EditKind::Other, {offset, offset + length});
	}

	UndoStack::Step step;
	step.offset = offset;
	step.inserted = text;
	const EditRecord edit = ApplyEdit(offset, length, step.inserted, step.removed);

	if (!fGroupHasSteps) {
		fUndo->BeginGroup(fGroupName, fGroupOwner, fGroupKind, fGroupBefore, std::move(step));
		fGroupHasSteps = true;
	} else {
		fUndo->Append(std::move(step));
	}

	if (implicitGroup) {
		fGroupAfter = {edit.NewEnd(), edit.NewEnd()};
		EndGroup();
	} else {
		fGroupAfter = MapSelection(fGroupAfter, edit);
	}
}


void TextModel::SetText(std::string_view text)
{
	AssertLocked();
	ResetText(Rope(text));
}


// Replaces the whole text without undo; marks keep their line numbers and
// decorations their lines and columns (clamped). The old text is not copied,
// as undo is cleared.
void TextModel::ResetText(const Rope &text)
{
	if (fGroupDepth > 0) {
		throw std::logic_error("TextModel: text reset inside an edit group");
	}

	std::vector<std::vector<size_t>> markLines;
	for (const MarkSet *set : fMarkSets) {
		markLines.push_back(set->Lines(fText));
	}
	std::vector<std::vector<std::pair<TextPoint, TextPoint>>> decorationPoints;
	for (const DecorationSet *set : fDecorationSets) {
		decorationPoints.push_back(set->Points(fText));
	}

	EditRecord edit;
	edit.offset = 0;
	edit.oldLength = fText.Length();
	edit.newLength = text.Length();
	edit.oldEnd = EndPoint(fText);
	edit.newEnd = EndPoint(text);
	fText = text;
	edit.revision = ++fRevision;
	if (fSyntax != nullptr) {
		fSyntax->TextEdited(edit);
	}
	fLog.push_back(edit);
	TrimLog();

	for (size_t i = 0; i < fMarkSets.size(); i++) {
		fMarkSets[i]->RestoreLines(fText, markLines[i]);
	}
	for (size_t i = 0; i < fDecorationSets.size(); i++) {
		fDecorationSets[i]->RestorePoints(fText, decorationPoints[i]);
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


std::string TextModel::UndoName() const
{
	AssertLocked();
	return fUndo->CanUndo() ? fUndo->TopUndo().name : std::string();
}


std::string TextModel::RedoName() const
{
	AssertLocked();
	return fUndo->CanRedo() ? fUndo->TopRedo().name : std::string();
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

ObserverId TextModel::AddObserver()
{
	AssertLocked();
	const ObserverId id = fNextObserverId++;
	Observer &observer = fObservers[id];
	observer.revision = fRevision;
	observer.overlaysRevision = fOverlaysRevision;
	observer.syntaxRevision = fSyntaxRevision;
	return id;
}


ObserverId TextModel::AddObserver(const BMessenger &messenger)
{
	const ObserverId id = AddObserver();
	SetObserverMessenger(id, messenger);
	return id;
}


void TextModel::SetObserverMessenger(ObserverId id, const BMessenger &messenger)
{
	AssertLocked();
	Observer &observer = fObservers.at(id);
	observer.messenger = messenger;
	observer.notify = true;
	observer.pending = false;
}


void TextModel::ClearObserverMessenger(ObserverId id)
{
	AssertLocked();
	Observer &observer = fObservers.at(id);
	observer.messenger = BMessenger();
	observer.notify = false;
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
	changes.overlaysChanged = observer.overlaysRevision != fOverlaysRevision;
	changes.syntaxChanged = observer.syntaxRevision != fSyntaxRevision;
	if (observer.syntaxRevision + 1 == fSyntaxRevision) {
		changes.syntaxRanges = fSyntaxRanges;
	}
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
	observer.overlaysRevision = fOverlaysRevision;
	observer.syntaxRevision = fSyntaxRevision;
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
		if (!observer.notify || observer.pending) {
			continue;
		}
		if (observer.revision == fRevision && observer.overlaysRevision == fOverlaysRevision
			&& observer.syntaxRevision == fSyntaxRevision) {
			continue;
		}
		observer.pending = true;
		notifications.emplace_back(id, observer.messenger);
	}
	return notifications;
}


//#pragma mark - Overlays

MarkSet &TextModel::SharedMarks() const
{
	return *fSharedMarks;
}


void TextModel::OverlaysChanged()
{
	fOverlaysRevision++;
}


//#pragma mark - File I/O

void TextModel::Load(BPositionIO &stream)
{
	AssertLocked();
	TextLoader loader;
	std::string buffer(kIOBufferSize, '\0');
	for (;;) {
		const ssize_t size = stream.Read(buffer.data(), buffer.size());
		if (size < 0) {
			CheckThrow(static_cast<status_t>(size));
		}
		if (size == 0) {
			break;
		}
		loader.Append(std::string_view(buffer.data(), size));
	}
	Load(loader);
}


void TextModel::Load(TextLoader &loader)
{
	AssertLocked();
	loader.Finish();
	ResetText(loader.TakeText());
	fLineEnding = loader.DominantLineEnding();
	fHasBom = loader.HasBom();
}


void TextModel::Load(const TextSnapshot &snapshot)
{
	AssertLocked();
	ResetText(snapshot.text);
	fLineEnding = snapshot.lineEnding;
	fHasBom = snapshot.hasBom;
}


// Writes at the stream's current position; the caller truncates if needed.
void TextModel::Save(BPositionIO &stream) const
{
	AssertLocked();
	Snapshot().Write([&stream](std::string_view data) {
		CheckThrow(stream.WriteExactly(data.data(), data.size()));
	});
}


TextSnapshot TextModel::Snapshot() const
{
	AssertLocked();
	return TextSnapshot {fText, fLineEnding, fHasBom};
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


//#pragma mark - Syntax

void TextModel::SetLanguage(const char *name)
{
	AssertLocked();
	const Language *language = name != nullptr ? Language::ForName(name) : nullptr;
	const Language *current = fSyntax != nullptr ? &fSyntax->GetLanguage() : nullptr;
	if (language == current) {
		return;
	}
	fSyntax.reset();
	if (language != nullptr) {
		fSyntax = std::make_unique<SyntaxState>(*this, *language);
	}
	SyntaxChanged();
}


const char *TextModel::LanguageName() const
{
	AssertLocked();
	return fSyntax != nullptr ? fSyntax->GetLanguage().Name() : nullptr;
}


uint64_t TextModel::SyntaxRevision() const
{
	AssertLocked();
	return fSyntaxRevision;
}


std::vector<OutlineItem> TextModel::Outline() const
{
	AssertLocked();
	if (fSyntax == nullptr || fSyntax->Tree() == nullptr) {
		return {};
	}
	QueryCursor cursor;
	return BuildOutline(fSyntax->GetLanguage(), fSyntax->Tree(), fText, cursor);
}


void TextModel::SyntaxChanged(std::optional<SyntaxRanges> ranges)
{
	AssertLocked();
	fSyntaxRevision++;
	fSyntaxRanges = std::move(ranges);
}


void TextModel::RetireWorkerThread(thread_id thread)
{
	AssertLocked();
	fRetiredWorkers.push_back(thread);
}


//#pragma mark - UndoScript

UndoScript::UndoScript(
	TextModel &model,
	std::string_view name,
	ObserverId owner,
	const Selection &before,
	EditKind kind
):
	fModel(model)
{
	fModel.BeginGroup(name, owner, kind, before);
}


UndoScript::~UndoScript()
{
	fModel.EndGroup();
}


} // CodeEdit
