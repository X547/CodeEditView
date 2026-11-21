#include <CodeEditView/TextCommands.h>

#include <stddef.h>

#include <algorithm>
#include <string>
#include <string_view>

#include <Beep.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>
#include <CodeEditView/EditorSettings.h>

#include "layout/LineLayout.h"


namespace CodeEdit {


namespace TextCommands {


static bool IsBlank(char ch)
{
	return ch == ' ' || ch == '\t';
}


static uint32_t TabWidth(const EditorSettings &settings)
{
	return std::max<uint32_t>(settings.tabWidth, 1);
}


static std::string LineText(const Rope &text, size_t line)
{
	const size_t start = text.LineStart(line);
	return text.Substring(start, text.LineEnd(line) - start);
}


//#pragma mark - Commands

std::vector<TextChange> CleanupWhitespace(const Rope &text, const EditorSettings &settings)
{
	(void)settings;
	std::vector<TextChange> changes;

	// The blanks and line breaks after the last other character form the end
	// of the text, replaced as a whole.
	size_t contentEnd = text.Length();
	while (contentEnd > 0 && (IsBlank(text.ByteAt(contentEnd - 1)) || text.ByteAt(contentEnd - 1) == '\n')) {
		contentEnd--;
	}
	if (contentEnd == 0) {
		if (!text.IsEmpty()) {
			changes.push_back({0, text.Length(), {}});
		}
		return changes;
	}

	const size_t lastLine = text.LineOfOffset(contentEnd);
	for (size_t line = 0; line < lastLine; line++) {
		const size_t start = text.LineStart(line);
		const std::string lineText = LineText(text, line);
		size_t keep = lineText.size();
		while (keep > 0 && IsBlank(lineText[keep - 1])) {
			keep--;
		}
		if (keep < lineText.size()) {
			changes.push_back({start + keep, lineText.size() - keep, {}});
		}
	}

	const size_t tailLength = text.Length() - contentEnd;
	if (tailLength != 1 || text.ByteAt(contentEnd) != '\n') {
		changes.push_back({contentEnd, tailLength, "\n"});
	}
	return changes;
}


std::vector<TextChange> TabsToSpaces(const Rope &text, const EditorSettings &settings)
{
	LayoutOptions options;
	options.tabWidth = TabWidth(settings);
	options.ambiguousWidth = settings.ambiguousWidth;

	std::vector<TextChange> changes;
	const size_t lineCount = text.LineCount();
	for (size_t line = 0; line < lineCount; line++) {
		const std::string lineText = LineText(text, line);
		if (lineText.find('\t') == std::string::npos) {
			continue;
		}
		// Tab stops are counted in cells, as the view shows them.
		const size_t start = text.LineStart(line);
		const LineLayout layout(lineText, options);
		for (const Cluster &cluster: layout.Clusters()) {
			if (cluster.kind == ClusterKind::Tab) {
				changes.push_back({start + cluster.offset, cluster.length, std::string(cluster.width, ' ')});
			}
		}
	}
	return changes;
}


std::vector<TextChange> LeadingSpacesToTabs(const Rope &text, const EditorSettings &settings)
{
	const uint32_t tabWidth = TabWidth(settings);

	std::vector<TextChange> changes;
	const size_t lineCount = text.LineCount();
	for (size_t line = 0; line < lineCount; line++) {
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
			changes.push_back({text.LineStart(line), leading, std::move(indent)});
		}
	}
	return changes;
}


//#pragma mark - Execution

size_t MapOffset(const std::vector<TextChange> &changes, size_t offset)
{
	ptrdiff_t delta = 0;
	for (const TextChange &change: changes) {
		if (offset < change.offset) {
			break;
		}
		const size_t end = change.offset + change.length;
		if (offset < end || (change.length == 0 && offset == change.offset)) {
			return change.offset + delta + std::min(offset - change.offset, change.text.size());
		}
		delta += static_cast<ptrdiff_t>(change.text.size()) - static_cast<ptrdiff_t>(change.length);
	}
	return offset + delta;
}


std::optional<Selection> Execute(
	TextModel &model,
	ObserverId owner,
	const TextCommand &command,
	const EditorSettings &settings,
	const Selection &before
)
{
	ModelLock lock(model);
	const std::vector<TextChange> changes = command(model.Text(), settings);
	if (changes.empty()) {
		return std::nullopt;
	}

	const Selection after {MapOffset(changes, before.anchor), MapOffset(changes, before.caret)};
	model.BeginGroup(owner, EditKind::Other, before);
	// Backwards, so the offsets of the changes still to make stay valid.
	for (size_t i = changes.size(); i-- > 0;) {
		model.Replace(changes[i].offset, changes[i].length, changes[i].text);
	}
	model.EndGroup(after);
	return after;
}


//#pragma mark - TextCommandSet

class TextCommandSet: public EditorCommandSet {
private:
	static TextCommand CommandOf(uint32 what);

public:
	bool Handles(uint32 what) const final;
	bool IsEnabled(uint32 what, CodeEditView *view) final;
	void Execute(BMessage *message, CodeEditView *view) final;
};


TextCommand TextCommandSet::CommandOf(uint32 what)
{
	switch (what) {
		case kMsgCleanupWhitespace:
			return CleanupWhitespace;
		case kMsgTabsToSpaces:
			return TabsToSpaces;
		case kMsgLeadingSpacesToTabs:
			return LeadingSpacesToTabs;
		default:
			break;
	}
	return {};
}


bool TextCommandSet::Handles(uint32 what) const
{
	return CommandOf(what) != nullptr;
}


bool TextCommandSet::IsEnabled(uint32 what, CodeEditView *view)
{
	(void)what;
	return view != nullptr && !view->Settings().readOnly;
}


void TextCommandSet::Execute(BMessage *message, CodeEditView *view)
{
	if (view == nullptr || view->Settings().readOnly) {
		beep();
		return;
	}
	const std::optional<Selection> after = TextCommands::Execute(
		*view->Model(),
		view->EditOwner(),
		CommandOf(message->what),
		view->Settings(),
		view->GetSelection()
	);
	if (after.has_value()) {
		view->SetSelection(*after);
	}
}


void AddItems(BMenu *menu)
{
	static TextCommandSet sCommands;
	EditorCommands::Register(&sCommands);

	menu->AddItem(new BMenuItem("Clean up whitespace", new BMessage(kMsgCleanupWhitespace)));
	menu->AddItem(new BMenuItem("Convert tabs to spaces", new BMessage(kMsgTabsToSpaces)));
	menu->AddItem(new BMenuItem("Convert leading spaces to tabs", new BMessage(kMsgLeadingSpacesToTabs)));
}


} // TextCommands


} // CodeEdit
