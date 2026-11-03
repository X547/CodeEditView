#include "KeyMap.h"

#include <InterfaceDefs.h>


namespace CodeEdit {


static constexpr uint32_t kModifierMask = B_SHIFT_KEY | B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY;


// Defaults follow Pe (Cmd is Alt on Haiku, Opt is the Windows key), with
// Control + navigation keys scrolling the view without moving the caret.
KeyMap KeyMap::Default()
{
	struct MovementBinding {
		uint32_t key;
		uint32_t modifiers;
		EditorCommand command;
	};
	static const MovementBinding kMovements[] = {
		{B_LEFT_ARROW,  0,             EditorCommand::MoveCharacterLeft},
		{B_RIGHT_ARROW, 0,             EditorCommand::MoveCharacterRight},
		{B_LEFT_ARROW,  B_COMMAND_KEY, EditorCommand::MoveWordLeft},
		{B_RIGHT_ARROW, B_COMMAND_KEY, EditorCommand::MoveWordRight},
		{B_LEFT_ARROW,  B_OPTION_KEY,  EditorCommand::MoveSubwordLeft},
		{B_RIGHT_ARROW, B_OPTION_KEY,  EditorCommand::MoveSubwordRight},
		{B_HOME,        0,             EditorCommand::MoveToLineStart},
		{B_END,         0,             EditorCommand::MoveToLineEnd},
		{B_UP_ARROW,    0,             EditorCommand::MoveToPreviousLine},
		{B_DOWN_ARROW,  0,             EditorCommand::MoveToNextLine},
		{B_PAGE_UP,     0,             EditorCommand::MoveToPageTop},
		{B_PAGE_DOWN,   0,             EditorCommand::MoveToPageBottom},
		{B_HOME,        B_COMMAND_KEY, EditorCommand::MoveToFileStart},
		{B_END,         B_COMMAND_KEY, EditorCommand::MoveToFileEnd},
		{B_HOME,        B_OPTION_KEY,  EditorCommand::MoveToFileStart},
		{B_END,         B_OPTION_KEY,  EditorCommand::MoveToFileEnd},
	};

	KeyMap keyMap;
	for (const MovementBinding &movement : kMovements) {
		keyMap.Add({movement.key, movement.modifiers, movement.command, false});
		keyMap.Add({movement.key, movement.modifiers | B_SHIFT_KEY, movement.command, true});
	}

	static const KeyBinding kOthers[] = {
		{B_BACKSPACE,   0,              EditorCommand::DeleteCharacterLeft},
		{B_BACKSPACE,   B_SHIFT_KEY,    EditorCommand::DeleteCharacterLeft},
		{B_BACKSPACE,   B_CONTROL_KEY,  EditorCommand::DeleteToLineStart},
		{B_DELETE,      0,              EditorCommand::DeleteCharacterRight},
		{B_DELETE,      B_SHIFT_KEY,    EditorCommand::DeleteCharacterRight},
		{B_DELETE,      B_CONTROL_KEY,  EditorCommand::DeleteToLineEnd},
		{B_UP_ARROW,    B_CONTROL_KEY,  EditorCommand::ScrollLineUp},
		{B_DOWN_ARROW,  B_CONTROL_KEY,  EditorCommand::ScrollLineDown},
		{B_UP_ARROW,    B_OPTION_KEY,   EditorCommand::ScrollLineUp},
		{B_DOWN_ARROW,  B_OPTION_KEY,   EditorCommand::ScrollLineDown},
		{B_LEFT_ARROW,  B_CONTROL_KEY,  EditorCommand::ScrollColumnLeft},
		{B_RIGHT_ARROW, B_CONTROL_KEY,  EditorCommand::ScrollColumnRight},
		{B_PAGE_UP,     B_CONTROL_KEY,  EditorCommand::ScrollPageUp},
		{B_PAGE_DOWN,   B_CONTROL_KEY,  EditorCommand::ScrollPageDown},
		{B_HOME,        B_CONTROL_KEY,  EditorCommand::ScrollToStart},
		{B_END,         B_CONTROL_KEY,  EditorCommand::ScrollToEnd},
		{'z',           B_COMMAND_KEY,  EditorCommand::Undo},
		{'z',           B_COMMAND_KEY | B_SHIFT_KEY, EditorCommand::Redo},
		{'a',           B_COMMAND_KEY,  EditorCommand::SelectAll},
		// Pe binds Balance to Cmd+B; here it expands by syntax node.
		{'b',           B_COMMAND_KEY,  EditorCommand::ExpandSelection},
		{'b',           B_COMMAND_KEY | B_SHIFT_KEY, EditorCommand::ShrinkSelection},
		{B_UP_ARROW,    B_COMMAND_KEY,  EditorCommand::MoveToPreviousDefinition},
		{B_DOWN_ARROW,  B_COMMAND_KEY,  EditorCommand::MoveToNextDefinition},
		{'.',           B_COMMAND_KEY,  EditorCommand::ToggleFold},
	};
	for (const KeyBinding &binding : kOthers) {
		keyMap.Add(binding);
	}
	return keyMap;
}


void KeyMap::Add(const KeyBinding &binding)
{
	fBindings.push_back(binding);
}


std::optional<KeyBinding> KeyMap::Find(uint32_t key, uint32_t modifiers) const
{
	const uint32_t maskedModifiers = modifiers & kModifierMask;
	for (const KeyBinding &binding : fBindings) {
		if (binding.key == key && binding.modifiers == maskedModifiers) {
			return binding;
		}
	}
	return std::nullopt;
}


} // CodeEdit
