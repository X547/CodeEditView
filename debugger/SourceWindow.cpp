#include "SourceWindow.h"

#include <exception>
#include <string>

#include <Alert.h>
#include <Application.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>

#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>

#include "Common.h"


namespace Debugger {


static constexpr uint32 kMsgToggleBreakpoint = 'SWtb';
static constexpr uint32 kMsgToggleBreakpointEnabled = 'SWte';
static constexpr uint32 kMsgSave = 'SWsv';
static constexpr uint32 kMsgReveal = 'SWrv';


static BRect NextWindowFrame()
{
	static int32 count = 0;
	const float offset = 24 * (count++ % 8);
	return BRect(360 + offset, 80 + offset, 1059 + offset, 679 + offset);
}


SourceWindow::SourceWindow(SourceDocument *document):
	BWindow(NextWindowFrame(), "Source", B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(document),
	fEditView(new CodeEditView("source", document->model.Get()))
{
	SetTitle(BPath(document->path.c_str()).Leaf());
	fEditView->SetTarget(BMessenger(this));
	CodeEdit::MarkStyle frameStyle;
	frameStyle.shape = CodeEdit::MarkShape::Arrow;
	frameStyle.color = {60, 170, 80, 255};
	frameStyle.lineBackground = {60, 170, 80, 64};
	frameStyle.priority = 25;
	fEditView->SetMarkStyle(kMarkFrameLine, frameStyle);

	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Save", kMsgSave, 'S')
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
		.End()
		.AddMenu("Edit")
			.AddItem("Undo", B_UNDO, 'Z')
			.AddItem("Redo", B_REDO, 'Z', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Cut", B_CUT, 'X')
			.AddItem("Copy", B_COPY, 'C')
			.AddItem("Paste", B_PASTE, 'V')
			.AddSeparator()
			.AddItem("Select all", B_SELECT_ALL, 'A')
		.End()
		.AddMenu("Breakpoint")
			.AddItem("Toggle breakpoint", kMsgToggleBreakpoint, 'K')
			.AddItem("Enable/disable breakpoint", kMsgToggleBreakpointEnabled, 'K', B_SHIFT_KEY)
		.End();
	menuBar->AddItem(CreateDebugMenu());
	menuBar->AddItem(CreateWindowsMenu());

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(-1, -1, -1, -1)
			.Add(new CodeEditScrollView("pane", fEditView))
		.End();
	fEditView->MakeFocus(true);
}


void SourceWindow::RevealLine(int32 line)
{
	// After showing a new window, its views are laid out first.
	BMessage message(kMsgReveal);
	message.AddInt32("line", line);
	PostMessage(&message);
}


void SourceWindow::Save()
{
	BFile file(fDocument->path.c_str(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK) {
		(new BAlert("Save", ("Cannot write " + fDocument->path).c_str(), "OK"))->Go(nullptr);
		return;
	}
	try {
		CodeEdit::ModelLock lock(*fDocument->model);
		fDocument->model->Save(file);
		fDocument->model->SetSavePoint();
	} catch (const std::exception &e) {
		(new BAlert("Save", e.what(), "OK"))->Go(nullptr);
	}
}


void SourceWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgReveal:
			if (message->GetInt32("line", 0) > 0) {
				fEditView->RevealLine(message->GetInt32("line", 1) - 1);
			}
			break;
		case kMsgToggleBreakpoint:
			ToggleBreakpoint(*fDocument->model, fEditView->CaretLocation().line);
			break;
		case kMsgToggleBreakpointEnabled:
			ToggleBreakpointEnabled(*fDocument->model, fEditView->CaretLocation().line);
			break;
		case CodeEdit::kMsgGutterClicked: {
			// Click: breakpoint; Shift + click or secondary button: enable or
			// disable it.
			const size_t line = message->GetUInt64("line", 0);
			const bool alternate = (message->GetInt32("modifiers", 0) & B_SHIFT_KEY) != 0
				|| (message->GetInt32("buttons", 0) & B_SECONDARY_MOUSE_BUTTON) != 0;
			if (alternate) {
				ToggleBreakpointEnabled(*fDocument->model, line);
			} else {
				ToggleBreakpoint(*fDocument->model, line);
			}
			break;
		}
		case kMsgSave:
			Save();
			break;
		case B_UNDO:
		case B_REDO:
		case B_CUT:
		case B_COPY:
		case B_PASTE:
		case B_SELECT_ALL:
			PostMessage(message, fEditView);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool SourceWindow::QuitRequested()
{
	BMessage closed(kMsgSourceWindowClosed);
	closed.AddString("path", fDocument->path.c_str());
	be_app->PostMessage(&closed);
	return true;
}


} // Debugger
