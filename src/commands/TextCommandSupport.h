#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include <CodeEditView/EditorSettings.h>
#include <CodeEditView/TextModel.h>


namespace CodeEdit {


namespace TextCommands {


inline bool IsBlank(char ch)
{
	return ch == ' ' || ch == '\t';
}


inline uint32_t TabWidth(const EditorSettings &settings)
{
	return std::max<uint32_t>(settings.tabWidth, 1);
}


// Without the line break.
inline std::string LineText(const Rope &text, size_t line)
{
	const size_t start = text.LineStart(line);
	return text.Substring(start, text.LineEnd(line) - start);
}


// Locks the model and runs edit() in an undo script; returns the selection
// after the script, or nothing if the text did not change.
template<typename Edit>
std::optional<Selection> RunScript(
	TextModel &model,
	std::string_view name,
	ObserverId owner,
	const Selection &selection,
	const Edit &edit
)
{
	ModelLock lock(model);
	const uint64_t revision = model.Revision();
	Selection after;
	{
		UndoScript script(model, name, owner, selection);
		edit();
		after = script.After();
	}
	if (model.Revision() == revision) {
		return std::nullopt;
	}
	return after;
}


} // TextCommands


} // CodeEdit
