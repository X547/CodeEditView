#pragma once

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <Referenceable.h>

#include <CodeEditView/EditRecord.h>
#include <CodeEditView/Rope.h>


class BPositionIO;


namespace CodeEdit {


class UndoStack;


// Posted to observers when the model changed since their last Sync(). At most
// one message is pending per observer. Field "model" (pointer): the model.
constexpr uint32_t kMsgModelChanged = 'CEmc';

using ObserverId = int32_t;
using MarkId = int32_t;

constexpr ObserverId kNoObserver = 0;

constexpr uint32_t kMarkBookmark = 1;
constexpr uint32_t kMarkBreakpoint = 2;
constexpr uint32_t kMarkExecLine = 3;
constexpr uint32_t kMarkFirstUserKind = 1000;


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


enum class LineEnding {
	LF,
	CRLF,
	CR,
};


// What happens to a mark whose line start is deleted (lines joined).
enum class MarkDeletePolicy {
	Remove,
	MoveToEditLine,
};


struct MarkInfo {
	MarkId id {};
	uint32_t kind {};
	size_t line {};
	int64_t data {};
};


struct ModelChanges {
	uint64_t revision {};
	// Edits are not available (log trimmed); the observer must rebuild all state.
	bool reset {};
	bool marksChanged {};
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
		uint64_t revision {};
		uint64_t marksRevision {};
		bool pending {};
	};

	struct Mark {
		uint32_t kind {};
		size_t anchor {};
		int64_t data {};
		MarkDeletePolicy policy {};
	};

	using Notification = std::pair<ObserverId, BMessenger>;

	mutable BLocker fLock;
	Rope fText;
	uint64_t fRevision {};
	uint64_t fMarksRevision {};
	std::deque<EditRecord> fLog;

	std::map<ObserverId, Observer> fObservers;
	ObserverId fNextObserverId = 1;

	std::map<MarkId, Mark> fMarks;
	MarkId fNextMarkId = 1;

	std::unique_ptr<UndoStack> fUndo;
	int32_t fGroupDepth {};
	ObserverId fGroupOwner {};
	EditKind fGroupKind {};
	Selection fGroupBefore;
	bool fGroupHasSteps {};

	LineEnding fLineEnding = LineEnding::LF;
	bool fHasBom {};

	void AssertLocked() const;
	void CheckRange(size_t offset, size_t length) const;
	EditRecord ApplyTextEdit(size_t offset, size_t length, std::string_view text, std::string &removed);
	EditRecord ApplyEdit(size_t offset, size_t length, std::string_view text, std::string &removed);
	std::vector<MarkId> WholeLineDeletedMarks(size_t offset, size_t length) const;
	void UpdateMarks(const EditRecord &edit, const std::vector<MarkId> &deletedLineMarks);
	void ResetText(std::string_view text);
	void TrimLog();
	std::vector<Notification> CollectNotifications();

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

	// Observers.
	ObserverId AddObserver(const BMessenger &messenger);
	void RemoveObserver(ObserverId id);
	int32_t CountObservers() const;
	ModelChanges Sync(ObserverId id);

	// Marks.
	MarkId AddMark(uint32_t kind, size_t line, int64_t data = 0, MarkDeletePolicy policy = MarkDeletePolicy::Remove);
	bool RemoveMark(MarkId id);
	void RemoveMarks(uint32_t kind);
	bool MoveMark(MarkId id, size_t line);
	std::optional<MarkInfo> FindMark(MarkId id) const;
	std::vector<MarkInfo> Marks(size_t firstLine, size_t lastLine) const;

	// File I/O. Text is stored with LF line endings; the dominant line ending
	// and a UTF-8 BOM are detected on load and restored on save.
	void Load(BPositionIO &stream);
	void Save(BPositionIO &stream) const;
	LineEnding GetLineEnding() const;
	void SetLineEnding(LineEnding lineEnding);
	bool HasBom() const;
	void SetHasBom(bool hasBom);
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
