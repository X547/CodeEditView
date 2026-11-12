#include "ConsoleWindow.h"

#include <string.h>

#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <TextControl.h>
#include <TextView.h>


namespace Debugger {


static constexpr uint32 kMsgInput = 'CWin';
static constexpr int32 kMaxOutputLength = 1 << 20;


ConsoleWindow::ConsoleWindow(const SessionContext &context):
	ToolWindow(BRect(40, 520, 679, 799), "Console", context),
	fOutput(new BTextView("output")),
	fInput(new BTextControl("input", "GDB:", "", new BMessage(kMsgInput)))
{
	fOutput->MakeEditable(false);
	fOutput->SetStylable(true);
	fOutput->SetWordWrap(true);
	BFont font(be_fixed_font);
	fOutput->SetFontAndColor(&font);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(-1, -1, -1, 0)
			.Add(new BScrollView("outputScroll", fOutput, 0, false, true))
		.End()
		.AddGroup(B_HORIZONTAL)
			.SetInsets(B_USE_HALF_ITEM_INSETS, 0, B_USE_HALF_ITEM_INSETS, B_USE_HALF_ITEM_INSETS)
			.Add(fInput)
		.End();
	fInput->MakeFocus(true);
}


void ConsoleWindow::Append(const char *text, const char *category)
{
	rgb_color color = ui_color(B_DOCUMENT_TEXT_COLOR);
	if (strcmp(category, "important") == 0 || strcmp(category, "stderr") == 0) {
		color = {200, 40, 40, 255};
	} else if (strcmp(category, "input") == 0) {
		color = {40, 100, 200, 255};
	} else if (strcmp(category, "console") == 0) {
		color = mix_color(ui_color(B_DOCUMENT_BACKGROUND_COLOR), color, 160);
	}
	BFont font(be_fixed_font);
	const bool atEnd = fOutput->Bounds().bottom >= fOutput->TextRect().bottom - 2;
	fOutput->Insert(fOutput->TextLength(), text, strlen(text), nullptr);
	fOutput->SetFontAndColor(fOutput->TextLength() - strlen(text), fOutput->TextLength(), &font, B_FONT_ALL, &color);
	if (fOutput->TextLength() > kMaxOutputLength) {
		fOutput->Delete(0, fOutput->TextLength() - kMaxOutputLength / 2);
	}
	if (atEnd) {
		fOutput->ScrollToOffset(fOutput->TextLength());
	}
}


void ConsoleWindow::SessionChanged()
{
	fOutput->SetText("");
}


void ConsoleWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kNoticeOutput: {
			const char *text;
			for (int32 i = 0; message->FindString("text", i, &text) == B_OK; i++) {
				Append(text, message->GetString("category", i, "console"));
			}
			break;
		}
		case kMsgInput: {
			const char *text = fInput->Text();
			if (text[0] == '\0') {
				break;
			}
			BMessage evaluate(kMsgEvaluate);
			evaluate.AddString("expression", text);
			Session().SendMessage(&evaluate);
			fInput->SetText("");
			break;
		}
		case kNoticeState:
		case kNoticeThreads:
		case kNoticeStack:
		case kNoticeScopes:
		case kNoticeVariables:
		case kNoticeBreakpoints:
			break;
		default:
			ToolWindow::MessageReceived(message);
			break;
	}
}


} // Debugger
