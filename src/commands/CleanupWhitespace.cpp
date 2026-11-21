#include <CodeEditView/TextCommands.h>

#include "TextCommandSupport.h"


namespace CodeEdit {


namespace TextCommands {


std::optional<Selection> CleanupWhitespace(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
)
{
	(void)settings;
	return RunScript(model, "Clean up whitespace", owner, selection, [&model]() {
		const Rope &text = model.Text();

		// The blanks and line breaks after the last other character form the
		// end of the text, replaced as a whole.
		size_t contentEnd = text.Length();
		while (contentEnd > 0 && (IsBlank(text.ByteAt(contentEnd - 1)) || text.ByteAt(contentEnd - 1) == '\n')) {
			contentEnd--;
		}
		if (contentEnd == 0) {
			model.Erase(0, text.Length());
			return;
		}
		const size_t tailLength = text.Length() - contentEnd;
		if (tailLength != 1 || text.ByteAt(contentEnd) != '\n') {
			model.Replace(contentEnd, tailLength, "\n");
		}

		// The lines before the last one with content, from the end, so the
		// offsets of earlier lines stay valid.
		for (size_t line = text.LineOfOffset(contentEnd); line-- > 0;) {
			const size_t start = text.LineStart(line);
			const std::string lineText = LineText(text, line);
			size_t keep = lineText.size();
			while (keep > 0 && IsBlank(lineText[keep - 1])) {
				keep--;
			}
			model.Erase(start + keep, lineText.size() - keep);
		}
	});
}


} // TextCommands


} // CodeEdit
