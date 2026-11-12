#include "GoToLinePanel.h"

#include <Beep.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <TextControl.h>

#include <CodeEditView/CodeEditView.h>

#include "FindCommands.h"


namespace FindKit {


static constexpr uint32 kMsgGo = 'FKgo';


// A positive decimal number filling the text.
static std::optional<size_t> ParseNumber(const std::string &text)
{
	if (text.empty() || text.size() > 18) {
		return std::nullopt;
	}
	size_t value = 0;
	for (const char c : text) {
		if (c < '0' || c > '9') {
			return std::nullopt;
		}
		value = value * 10 + static_cast<size_t>(c - '0');
	}
	if (value == 0) {
		return std::nullopt;
	}
	return value;
}


std::optional<CodeEdit::TextPoint> ParseLocation(const std::string &text)
{
	const size_t start = text.find_first_not_of(" \t");
	const size_t end = text.find_last_not_of(" \t");
	if (start == std::string::npos) {
		return std::nullopt;
	}
	const std::string trimmed = text.substr(start, end - start + 1);
	const size_t colon = trimmed.find(':');
	const std::optional<size_t> line = ParseNumber(trimmed.substr(0, colon));
	if (!line.has_value()) {
		return std::nullopt;
	}
	size_t column = 1;
	if (colon != std::string::npos) {
		const std::optional<size_t> parsed = ParseNumber(trimmed.substr(colon + 1));
		if (!parsed.has_value()) {
			return std::nullopt;
		}
		column = *parsed;
	}
	return CodeEdit::TextPoint {*line - 1, column - 1};
}


GoToLinePanel::GoToLinePanel(CodeEditView *editor):
	BWindow(
		BRect(0, 0, 100, 100),
		"Go to line",
		B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_CLOSE_ON_ESCAPE
	),
	fHost(editor->Window())
{
	const CodeEdit::TextPoint caret = editor->CaretLocation();
	const std::string current = std::to_string(caret.line + 1) + ":" + std::to_string(caret.column + 1);
	fLocation = new BTextControl("location", "Line[:column]:", current.c_str(), nullptr);
	BButton *cancel = new BButton("Cancel", new BMessage(B_QUIT_REQUESTED));
	BButton *go = new BButton("Go", new BMessage(kMsgGo));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fLocation)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(go)
		.End();
	SetDefaultButton(go);
	fLocation->MakeFocus(true);
	fLocation->TextView()->SelectAll();
	ResizeToPreferred();
	if (editor->Window() != nullptr) {
		CenterIn(editor->Window()->Frame());
	}
	MoveOnScreen(B_MOVE_IF_PARTIALLY_OFFSCREEN);
}


void GoToLinePanel::MessageReceived(BMessage *message)
{
	if (message->what != kMsgGo) {
		BWindow::MessageReceived(message);
		return;
	}
	const std::optional<CodeEdit::TextPoint> location = ParseLocation(fLocation->Text());
	if (!location.has_value()) {
		beep();
		fLocation->MakeFocus(true);
		return;
	}
	BMessage go(kMsgGoToLine);
	go.AddInt32("line", static_cast<int32>(location->line + 1));
	go.AddInt32("column", static_cast<int32>(location->column + 1));
	fHost.SendMessage(&go);
	Quit();
}


} // FindKit
