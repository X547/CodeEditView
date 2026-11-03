#pragma once

#include <stdint.h>

#include <optional>
#include <vector>


namespace CodeEdit {


enum class EditorCommand {
	MoveCharacterLeft,
	MoveCharacterRight,
	MoveWordLeft,
	MoveWordRight,
	MoveSubwordLeft,
	MoveSubwordRight,
	MoveToLineStart,
	MoveToLineEnd,
	MoveToPreviousLine,
	MoveToNextLine,
	MoveToPageTop,
	MoveToPageBottom,
	MoveToFileStart,
	MoveToFileEnd,
	DeleteCharacterLeft,
	DeleteCharacterRight,
	DeleteToLineStart,
	DeleteToLineEnd,
	ScrollLineUp,
	ScrollLineDown,
	ScrollColumnLeft,
	ScrollColumnRight,
	ScrollPageUp,
	ScrollPageDown,
	ScrollToStart,
	ScrollToEnd,
	Undo,
	Redo,
	SelectAll,
	ExpandSelection,
	ShrinkSelection,
	MoveToPreviousDefinition,
	MoveToNextDefinition,
	ToggleFold,
	FoldAll,
	UnfoldAll,
};


struct KeyBinding {
	// The "raw_char" of the key, unaffected by modifiers.
	uint32_t key {};
	// Combination of B_SHIFT_KEY, B_COMMAND_KEY, B_CONTROL_KEY, B_OPTION_KEY.
	uint32_t modifiers {};
	EditorCommand command {};
	// Movement extends the selection.
	bool extend {};
};


class KeyMap {
private:
	std::vector<KeyBinding> fBindings;

public:
	static KeyMap Default();

	void Add(const KeyBinding &binding);
	const std::vector<KeyBinding> &Bindings() const {return fBindings;}
	std::optional<KeyBinding> Find(uint32_t key, uint32_t modifiers) const;
};


} // CodeEdit
