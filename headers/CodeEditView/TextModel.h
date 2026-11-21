#pragma once

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <OS.h>
#include <Referenceable.h>

#include <CodeEditView/EditRecord.h>
#include <CodeEditView/Outline.h>
#include <CodeEditView/Overlays.h>
#include <CodeEditView/Rope.h>
#include <CodeEditView/TextLoader.h>


class BPositionIO;


namespace CodeEdit {


class EditorCore;
class SyntaxState;
class SyntaxWorker;
class UndoScript;
class UndoStack;


// Posted to observers when the model changed since their last Sync(). At most
// one message is pending per observer. Field "model" (pointer): the model.
constexpr uint32_t kMsgModelChanged = 'CEmc';

using ObserverId = int32_t;

constexpr ObserverId kNoObserver = 0;


struct Selection {
	size_t anchor {};
	size_t caret {};

	bool operator==(const Selection &other) const = default;
};


// The selection after the edit: text inserted at its edges stays outside of
// it, a caret moves after text inserted at it.
Selection MapSelection(const Selection &selection, const EditRecord &edit);


// Consecutive undo steps of the same kind and owner that continue each other
// are merged into one.
enum class EditKind {
	Other,
	Typing,
	Backspace,
	Delete,
};


// The text of a model as saved, taken at one moment. Taking one is cheap (the
// rope shares its nodes) and it may be used in any thread without the model.
struct TextSnapshot {
	Rope text;
	LineEnding lineEnding = LineEnding::LF;
	bool hasBom {};

	// Passes the bytes of the file to write in pieces, in order.
	void Write(const std::function<void(std::string_view data)> &write) const;
};


// Byte ranges of the text whose syntax changed with a new syntax tree, as
// [start, end), in the text of revision (later edits move them).
struct SyntaxRanges {
	uint64_t revision {};
	std::vector<std::pair<size_t, size_t>> ranges;
};


struct ModelChanges {
	uint64_t revision {};
	// Edits are not available (log trimmed); the observer must rebuild all state.
	bool reset {};
	// A mark set or decoration set of the model changed (the Revision() of
	// the sets tells which).
	bool overlaysChanged {};
	// A new syntax tree changed the highlighting.
	bool syntaxChanged {};
	// With syntaxChanged: where it changed, if known (one new tree since the
	// last Sync()); otherwise anything may have changed.
	std::optional<SyntaxRanges> syntaxRanges;
	std::vector<EditRecord> edits;
};


struct UndoResult {
	ObserverId owner {};
	Selection selection;
};


// Shared text document. All methods except Lock() require the model lock.
//
// Lock order: a looper lock may be held while locking the model, never the
// other way around. The model only posts messages, after releasing its lock.
class TextModel: public BReferenceable {
private:
	struct Observer {
		BMessenger messenger;
		bool notify {};
		uint64_t revision {};
		uint64_t overlaysRevision {};
		uint64_t syntaxRevision {};
		bool pending {};
	};

	using Notification = std::pair<ObserverId, BMessenger>;

	mutable BLocker fLock;
	Rope fText;
	uint64_t fRevision {};
	// Changes with any mark set or decoration set.
	uint64_t fOverlaysRevision {};
	std::deque<EditRecord> fLog;

	std::map<ObserverId, Observer> fObservers;
	ObserverId fNextObserverId = 1;

	// Attached sets, including fSharedMarks.
	std::vector<MarkSet*> fMarkSets;
	std::vector<DecorationSet*> fDecorationSets;
	std::unique_ptr<MarkSet> fSharedMarks;

	std::unique_ptr<UndoStack> fUndo;
	// The open undo script; nested scripts only count the depth.
	int32_t fGroupDepth {};
	ObserverId fGroupOwner {};
	EditKind fGroupKind {};
	std::string fGroupName;
	Selection fGroupBefore;
	// The selection after the edits so far.
	Selection fGroupAfter;
	bool fGroupHasSteps {};

	LineEnding fLineEnding = LineEnding::LF;
	bool fHasBom {};
	// The line ending or BOM changed since the save point.
	bool fFormatChanged {};

	std::unique_ptr<SyntaxState> fSyntax;
	uint64_t fSyntaxRevision {};
	// Where the last syntax change changed the syntax; none if unknown.
	std::optional<SyntaxRanges> fSyntaxRanges;
	std::vector<thread_id> fRetiredWorkers;

	friend class EditorCore;
	friend class SyntaxState;
	friend class SyntaxWorker;
	friend class MarkSet;
	friend class DecorationSet;
	friend class UndoScript;

	void AssertLocked() const;
	void CheckRange(size_t offset, size_t length) const;
	void BeginGroup(std::string_view name, ObserverId owner, EditKind kind, const Selection &before);
	void EndGroup();
	EditRecord ApplyTextEdit(size_t offset, size_t length, std::string_view text, std::string &removed);
	EditRecord ApplyEdit(size_t offset, size_t length, std::string_view text, std::string &removed);
	void ResetText(const Rope &text);
	void OverlaysChanged();
	void TrimLog();
	std::vector<Notification> CollectNotifications();

	SyntaxState *Syntax() const {return fSyntax.get();}
	void SyntaxChanged(std::optional<SyntaxRanges> ranges = std::nullopt);
	void RetireWorkerThread(thread_id thread);

public:
	TextModel();
	~TextModel() override;

	bool Lock();
	void Unlock();
	bool IsLocked() const;

	const Rope &Text() const;
	uint64_t Revision() const;

	// Text editing. An edit outside of an UndoScript is an undo step of its
	// own, without name and owner.
	void Replace(size_t offset, size_t length, std::string_view text);
	void Insert(size_t offset, std::string_view text) {Replace(offset, 0, text);}
	void Erase(size_t offset, size_t length) {Replace(offset, length, {});}
	void SetText(std::string_view text);

	// Undo.
	bool CanUndo() const;
	bool CanRedo() const;
	std::optional<UndoResult> Undo();
	std::optional<UndoResult> Redo();
	// Names of the steps Undo() and Redo() would take back or make again;
	// empty if there is none or it has no name.
	std::string UndoName() const;
	std::string RedoName() const;
	void BreakUndoCoalescing();
	bool IsModified() const;
	void SetSavePoint();

	// Observers. An observer without messenger is never notified and only
	// learns about changes by calling Sync().
	ObserverId AddObserver();
	ObserverId AddObserver(const BMessenger &messenger);
	void SetObserverMessenger(ObserverId id, const BMessenger &messenger);
	void ClearObserverMessenger(ObserverId id);
	void RemoveObserver(ObserverId id);
	int32_t CountObservers() const;
	ModelChanges Sync(ObserverId id);

	// Marks shown by every view of the model (bookmarks, breakpoints). It
	// lives as long as the model; do not take references to it.
	MarkSet &SharedMarks() const;

	// File I/O. Text is stored with LF line endings; the dominant line ending
	// and a UTF-8 BOM are detected on load and restored on save.
	// Loading replaces the text without undo, like SetText().
	void Load(BPositionIO &stream);
	// Finishes the loader and takes its text.
	void Load(TextLoader &loader);
	// Shares the snapshot's text.
	void Load(const TextSnapshot &snapshot);
	void Save(BPositionIO &stream) const;
	TextSnapshot Snapshot() const;
	// Changing the line ending or the BOM is an unsaved change (IsModified())
	// until SetSavePoint() or a load; it is not undone.
	LineEnding GetLineEnding() const;
	void SetLineEnding(LineEnding lineEnding);
	bool HasBom() const;
	void SetHasBom(bool hasBom);

	// Syntax highlighting. The name is one of AvailableLanguages(); nullptr or
	// an unknown name selects plain text.
	void SetLanguage(const char *name);
	const char *LanguageName() const;
	uint64_t SyntaxRevision() const;
	// Definitions of the last syntax tree; empty without one.
	std::vector<OutlineItem> Outline() const;
};


// The edits of a model made while a script exists are one undo step with the
// script's name, like a script of operations in BlackBox. Undoing it restores
// the selection before of the owner, the view that made the edits
// (CodeEditView::EditOwner()); redoing it restores the selection after,
// which is the selection before mapped through the edits unless set.
//
// Scripts nest: the edits of an inner script belong to the outermost one,
// which gives the name, owner and kind. A script without edits makes no undo
// step. The model must stay locked while the script exists.
class UndoScript {
private:
	TextModel &fModel;

public:
	UndoScript(
		TextModel &model,
		std::string_view name,
		ObserverId owner,
		const Selection &before,
		EditKind kind = EditKind::Other
	);
	~UndoScript();

	UndoScript(const UndoScript &other) = delete;
	UndoScript &operator=(const UndoScript &other) = delete;

	// The selection after the edits so far; later edits move it.
	Selection After() const {return fModel.fGroupAfter;}
	void SetAfter(const Selection &after) {fModel.fGroupAfter = after;}
};


class ModelLock {
private:
	TextModel &fModel;

public:
	explicit ModelLock(TextModel &model):
		fModel(model)
	{
		fModel.Lock();
	}

	~ModelLock() {fModel.Unlock();}

	ModelLock(const ModelLock &other) = delete;
	ModelLock &operator=(const ModelLock &other) = delete;
};


} // CodeEdit
