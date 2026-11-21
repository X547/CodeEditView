#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include <CodeEditView/TextModel.h>


namespace CodeEdit {


class UndoStack {
public:
	struct Step {
		size_t offset {};
		std::string removed;
		std::string inserted;
	};

	struct Group {
		uint64_t id {};
		ObserverId owner {};
		EditKind kind {};
		std::string name;
		Selection before;
		Selection after;
		std::vector<Step> steps;
	};

private:
	std::vector<Group> fUndo;
	std::vector<Group> fRedo;
	uint64_t fNextId = 1;
	uint64_t fSavedId {};
	bool fCanCoalesce {};

	bool CanCoalesce(ObserverId owner, EditKind kind, const Step &step) const;
	uint64_t TopId() const {return fUndo.empty() ? 0 : fUndo.back().id;}

public:
	// Starts a new undo group with its first step, or continues the previous
	// group when the edit can be coalesced with it.
	void BeginGroup(std::string_view name, ObserverId owner, EditKind kind, const Selection &before, Step step);
	void Append(Step step);
	void EndGroup(const Selection &after);
	void BreakCoalescing() {fCanCoalesce = false;}

	bool CanUndo() const {return !fUndo.empty();}
	bool CanRedo() const {return !fRedo.empty();}
	const Group &TopUndo() const {return fUndo.back();}
	const Group &TopRedo() const {return fRedo.back();}
	Group PopUndo();
	Group PopRedo();
	void PushUndo(Group group);
	void PushRedo(Group group);

	bool IsModified() const {return TopId() != fSavedId;}
	void SetSavePoint();
	void Clear();
};


} // CodeEdit
