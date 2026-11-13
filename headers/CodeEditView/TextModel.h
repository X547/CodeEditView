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


// Consecutive edit groups of the same kind and owner that continue each other
// are merged into one undo step.
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


struct ModelChanges {
	uint64_t revision {};
	// Edits are not available (log trimmed); the observer must rebuild all state.
	bool reset {};
	// A mark set or decoration set of the model changed (the Revision() of
	// the sets tells which).
	bool overlaysChanged {};
	// A new syntax tree changed the highlighting.
	bool syntaxChanged {};
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
	int32_t fGroupDepth {};
	ObserverId fGroupOwner {};
	EditKind fGroupKind {};
	Selection fGroupBefore;
	bool fGroupHasSteps {};

	LineEnding fLineEnding = LineEnding::LF;
	bool fHasBom {};

	std::unique_ptr<SyntaxState> fSyntax;
	uint64_t fSyntaxRevision {};
	std::vector<thread_id> fRetiredWorkers;

	friend class EditorCore;
	friend class SyntaxState;
	friend class SyntaxWorker;
	friend class MarkSet;
	friend class DecorationSet;

	void AssertLocked() const;
	void CheckRange(size_t offset, size_t length) const;
	EditRecord ApplyTextEdit(size_t offset, size_t length, std::string_view text, std::string &removed);
	EditRecord ApplyEdit(size_t offset, size_t length, std::string_view text, std::string &removed);
	void ResetText(const Rope &text);
	void OverlaysChanged();
	void TrimLog();
	std::vector<Notification> CollectNotifications();

	SyntaxState *Syntax() const {return fSyntax.get();}
	void SyntaxChanged();
	void RetireWorkerThread(thread_id thread);

public:
	TextModel();
	~TextModel() override;

	bool Lock();
	void Unlock();
	bool IsLocked() const;

	const Rope &Text() const;
	uint64_t Revision() const;

	// Text editing.
	void BeginGroup(ObserverId owner, EditKind kind, const Selection &before);
	void EndGroup(const Selection &after);
	void Replace(size_t offset, size_t length, std::string_view text);
	void Insert(size_t offset, std::string_view text) {Replace(offset, 0, text);}
	void Erase(size_t offset, size_t length) {Replace(offset, length, {});}
	void SetText(std::string_view text);

	// Undo.
	bool CanUndo() const;
	bool CanRedo() const;
	std::optional<UndoResult> Undo();
	std::optional<UndoResult> Redo();
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
