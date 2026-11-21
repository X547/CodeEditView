#include <CodeEditView/TextCommands.h>

#include <string>
#include <vector>

#include "TextCommandSupport.h"
#include "layout/LineLayout.h"


namespace CodeEdit {


namespace TextCommands {


std::optional<Selection> TabsToSpaces(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
)
{
	LayoutOptions options;
	options.tabWidth = TabWidth(settings);
	options.ambiguousWidth = settings.ambiguousWidth;

	return RunScript(model, "Convert tabs to spaces", owner, selection, [&model, &options]() {
		const Rope &text = model.Text();
		// From the end, so the offsets of earlier tabs stay valid.
		for (size_t line = text.LineCount(); line-- > 0;) {
			const std::string lineText = LineText(text, line);
			if (lineText.find('\t') == std::string::npos) {
				continue;
			}
			// Tab stops are counted in cells, as the view shows them.
			const size_t start = text.LineStart(line);
			const LineLayout layout(lineText, options);
			const std::vector<Cluster> &clusters = layout.Clusters();
			for (auto cluster = clusters.rbegin(); cluster != clusters.rend(); ++cluster) {
				if (cluster->kind == ClusterKind::Tab) {
					model.Replace(start + cluster->offset, cluster->length, std::string(cluster->width, ' '));
				}
			}
		}
	});
}


} // TextCommands


} // CodeEdit
