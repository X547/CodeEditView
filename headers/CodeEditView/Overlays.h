#pragma once

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include <Referenceable.h>

#include <CodeEditView/EditRecord.h>
#include <CodeEditView/ViewTypes.h>


namespace CodeEdit {


class Rope;
class TextModel;


using MarkId = int32_t;

constexpr uint32_t kMarkBookmark = 1;
constexpr uint32_t kMarkBreakpoint = 2;
constexpr uint32_t kMarkExecLine = 3;
constexpr uint32_t kMarkBreakpointDisabled = 4;
constexpr uint32_t kMarkFirstUserKind = 1000;


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


// Line marks attached to a text model; they follow its edits. Every model has
// a set shown by all its views (TextModel::SharedMarks()); other sets are
// shown by the views given them (CodeEditView::SetMarkSets()), e.g. the
// execution line of one debug session. All methods require the model lock.
class MarkSet: public BReferenceable {
private:
	struct Mark {
		uint32_t kind {};
		size_t anchor {};
		int64_t data {};
		MarkDeletePolicy policy {};
	};

	TextModel *fModel;
	// The shared set of a model holds no reference to it.
	bool fOwnedByModel {};
	std::map<MarkId, Mark> fMarks;
	MarkId fNextId = 1;
	uint64_t fRevision {};

	friend class TextModel;

	MarkSet(TextModel &model, bool ownedByModel);

	void Changed();
	std::vector<MarkId> WholeLineDeletedMarks(const Rope &text, size_t offset, size_t length) const;
	void ApplyEdit(const Rope &text, const EditRecord &edit, const std::vector<MarkId> &deletedLineMarks);
	std::vector<size_t> Lines(const Rope &text) const;
	void RestoreLines(const Rope &text, const std::vector<size_t> &lines);

public:
	explicit MarkSet(TextModel &model);
	~MarkSet() override;

	MarkSet(const MarkSet &other) = delete;
	MarkSet &operator=(const MarkSet &other) = delete;

	TextModel &Model() const {return *fModel;}
	// Changes when marks are added, removed, moved or change kind.
	uint64_t Revision() const;

	MarkId AddMark(uint32_t kind, size_t line, int64_t data = 0, MarkDeletePolicy policy = MarkDeletePolicy::Remove);
	bool RemoveMark(MarkId id);
	void RemoveMarks(uint32_t kind);
	void Clear();
	bool MoveMark(MarkId id, size_t line);
	bool SetMarkKind(MarkId id, uint32_t kind);
	std::optional<MarkInfo> FindMark(MarkId id) const;
	// Marks of lines firstLine to lastLine, sorted by line and id.
	std::vector<MarkInfo> Marks(size_t firstLine, size_t lastLine) const;
};


// Decorations attached to a text model; they follow its edits and are shown
// by the views given them (CodeEditView::SetDecorationSets()), e.g. the
// coverage of one profile, shared by every view showing it. All methods
// require the model lock.
class DecorationSet: public BReferenceable {
private:
	TextModel *fModel;
	// Sorted by start.
	std::vector<Decoration> fDecorations;
	uint64_t fRevision {};

	friend class TextModel;

	void ApplyEdit(const EditRecord &edit);
	std::vector<std::pair<TextPoint, TextPoint>> Points(const Rope &text) const;
	void RestorePoints(const Rope &text, const std::vector<std::pair<TextPoint, TextPoint>> &points);

public:
	explicit DecorationSet(TextModel &model);
	~DecorationSet() override;

	DecorationSet(const DecorationSet &other) = delete;
	DecorationSet &operator=(const DecorationSet &other) = delete;

	TextModel &Model() const {return *fModel;}
	// Changes when the decorations are replaced.
	uint64_t Revision() const;

	// Offsets are clamped to the text.
	void SetDecorations(std::vector<Decoration> decorations);
	// Sorted by start.
	const std::vector<Decoration> &Decorations() const;
};


} // CodeEdit
