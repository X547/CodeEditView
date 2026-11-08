#include "BreakpointsWindow.h"

#include <string>

#include <Application.h>
#include <Button.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>

#include "Documents.h"


namespace Debugger {


static constexpr uint32 kMsgBreakpointSelected = 'BWse';
static constexpr uint32 kMsgBreakpointInvoked = 'BWiv';
static constexpr uint32 kMsgEnableBreakpoint = 'BWen';
static constexpr uint32 kMsgRemoveBreakpoint = 'BWrm';
static constexpr uint32 kMsgRemoveAllBreakpoints = 'BWra';

enum {
	kEnabledColumn,
	kFileColumn,
	kLineColumn,
	kStatusColumn,
};


class BreakpointRow: public BRow {
public:
	const std::string path;
	const int32 line;

	BreakpointRow(std::string path, int32 line, bool enabled, const char *status):
		path(std::move(path)),
		line(line)
	{
		SetField(new BStringField(enabled ? "\xe2\x97\x8f" : "\xe2\x97\x8b"), kEnabledColumn);
		SetField(new BStringField(FileName(this->path).c_str()), kFileColumn);
		SetField(new BStringField(std::to_string(line).c_str()), kLineColumn);
		SetField(new BStringField(status), kStatusColumn);
	}
};


BreakpointsWindow::BreakpointsWindow():
	ToolWindow(BRect(980, 80, 1379, 329), "Breakpoints"),
	fList(CreateColumnList("breakpoints", kMsgBreakpointSelected, kMsgBreakpointInvoked)),
	fEnableButton(new BButton("Enable/disable", new BMessage(kMsgEnableBreakpoint))),
	fRemoveButton(new BButton("Remove", new BMessage(kMsgRemoveBreakpoint)))
{
	fList->AddColumn(new BStringColumn("", 24, 20, 40, B_TRUNCATE_END, B_ALIGN_CENTER), kEnabledColumn);
	fList->AddColumn(new BStringColumn("File", 150, 40, 2000, B_TRUNCATE_BEGINNING), kFileColumn);
	fList->AddColumn(new BStringColumn("Line", 50, 30, 100, B_TRUNCATE_END, B_ALIGN_RIGHT), kLineColumn);
	fList->AddColumn(new BStringColumn("Status", 150, 40, 2000, B_TRUNCATE_END), kStatusColumn);
	fList->SetTarget(this);
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_HALF_ITEM_SPACING)
		.AddGroup(B_VERTICAL, 0)
			.SetInsets(-1, -1, -1, 0)
			.Add(fList)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_HALF_ITEM_SPACING)
			.SetInsets(B_USE_HALF_ITEM_INSETS, 0, B_USE_HALF_ITEM_INSETS, B_USE_HALF_ITEM_INSETS)
			.Add(fEnableButton)
			.Add(fRemoveButton)
			.AddGlue()
			.Add(new BButton("Remove all", new BMessage(kMsgRemoveAllBreakpoints)))
		.End();
	UpdateButtons();
}


void BreakpointsWindow::Update(const BMessage &notice)
{
	// Keep the selected breakpoint selected.
	std::string selectedPath;
	int32 selectedLine = 0;
	if (const BreakpointRow *selected = static_cast<BreakpointRow*>(fList->CurrentSelection())) {
		selectedPath = selected->path;
		selectedLine = selected->line;
	}
	fList->Clear();
	const char *path;
	for (int32 i = 0; notice.FindString("path", i, &path) == B_OK; i++) {
		const int32 line = notice.GetInt32("line", i, 0);
		const bool enabled = notice.GetBool("enabled", i, false);
		std::string status = "disabled";
		if (enabled) {
			const std::string message = notice.GetString("message", i, "");
			status = notice.GetBool("verified", i, false) ? "set" : (message.empty() ? "not set" : message);
		}
		BreakpointRow *row = new BreakpointRow(path, line, enabled, status.c_str());
		fList->AddRow(row);
		if (row->path == selectedPath && row->line == selectedLine) {
			fList->AddToSelection(row);
		}
	}
	UpdateButtons();
}


void BreakpointsWindow::UpdateButtons()
{
	const bool selected = fList->CurrentSelection() != nullptr;
	fEnableButton->SetEnabled(selected);
	fRemoveButton->SetEnabled(selected);
}


void BreakpointsWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kNoticeBreakpoints:
			Update(*message);
			break;
		case kMsgBreakpointSelected:
			UpdateButtons();
			break;
		case kMsgBreakpointInvoked: {
			const BreakpointRow *row = static_cast<BreakpointRow*>(fList->CurrentSelection());
			if (row != nullptr) {
				BMessage show(kMsgShowSource);
				show.AddString("path", row->path.c_str());
				show.AddInt32("line", row->line);
				be_app->PostMessage(&show);
			}
			break;
		}
		case kMsgEnableBreakpoint:
		case kMsgRemoveBreakpoint: {
			const BreakpointRow *row = static_cast<BreakpointRow*>(fList->CurrentSelection());
			const BReference<SourceDocument> document = row != nullptr
				? DocumentRegistry::Default().Find(row->path)
				: BReference<SourceDocument>();
			if (document == nullptr) {
				break;
			}
			if (message->what == kMsgEnableBreakpoint) {
				ToggleBreakpointEnabled(*document->model, row->line - 1);
			} else {
				ToggleBreakpoint(*document->model, row->line - 1);
			}
			break;
		}
		case kMsgRemoveAllBreakpoints:
			for (const BReference<SourceDocument> &document : DocumentRegistry::Default().All()) {
				CodeEdit::ModelLock lock(*document->model);
				document->model->SharedMarks().RemoveMarks(CodeEdit::kMarkBreakpoint);
				document->model->SharedMarks().RemoveMarks(CodeEdit::kMarkBreakpointDisabled);
			}
			break;
		case kNoticeState:
		case kNoticeOutput:
		case kNoticeThreads:
		case kNoticeStack:
		case kNoticeScopes:
		case kNoticeVariables:
			break;
		default:
			ToolWindow::MessageReceived(message);
			break;
	}
}


} // Debugger
