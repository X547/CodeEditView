#include <CodeEditView/TextCommands.h>

#include <string>
#include <string_view>

#include "TextCommandSupport.h"


namespace CodeEdit {


namespace TextCommands {


std::optional<Selection> LeadingSpacesToTabs(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
)
{
	const uint32_t tabWidth = TabWidth(settings);

	return RunScript(model, "Convert leading spaces to tabs", owner, selection, [&model, tabWidth]() {
		const Rope &text = model.Text();
		// From the end, so the offsets of earlier lines stay valid.
		for (size_t line = text.LineCount(); line-- > 0;) {
			const std::string lineText = LineText(text, line);
			size_t leading = 0;
			size_t width = 0;
			bool hasSpace = false;
			while (leading < lineText.size() && IsBlank(lineText[leading])) {
				if (lineText[leading] == ' ') {
					width++;
					hasSpace = true;
				} else {
					width += tabWidth - width % tabWidth;
				}
				leading++;
			}
			if (!hasSpace) {
				continue;
			}
			std::string indent(width / tabWidth, '\t');
			indent.append(width % tabWidth, ' ');
			if (std::string_view(lineText).substr(0, leading) != indent) {
				model.Replace(text.LineStart(line), leading, indent);
			}
		}
	});
}


} // TextCommands


} // CodeEdit
