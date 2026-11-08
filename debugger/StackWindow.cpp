#include "StackWindow.h"

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>

#include <string>


namespace Debugger {


static constexpr uint32 kMsgFrameSelected = 'SKse';
static constexpr uint32 kMsgFrameInvoked = 'SKiv';

enum {
	kIndexColumn,
	kFunctionColumn,
	kImageColumn,
	kLocationColumn,
};


class FrameRow: public BRow {
public:
	const int32 index;

	FrameRow(int32 index, const char *function, const std::string &image, const std::string &location):
		index(index)
	{
		SetField(new BStringField(std::to_string(index).c_str()), kIndexColumn);
		SetField(new BStringField(function), kFunctionColumn);
		SetField(new BStringField(image.c_str()), kImageColumn);
		SetField(new BStringField(location.c_str()), kLocationColumn);
	}
};


StackWindow::StackWindow(const SessionContext &context):
	ToolWindow(BRect(360, 360, 959, 559), "Stack", context),
	fList(CreateColumnList("stack", kMsgFrameSelected, kMsgFrameInvoked))
{
	fList->AddColumn(new BStringColumn("#", 30, 20, 60, B_TRUNCATE_END, B_ALIGN_RIGHT), kIndexColumn);
	fList->AddColumn(new BStringColumn("Function", 220, 50, 2000, B_TRUNCATE_END), kFunctionColumn);
	fList->AddColumn(new BStringColumn("Image", 130, 40, 1000, B_TRUNCATE_END), kImageColumn);
	fList->AddColumn(new BStringColumn("Location", 160, 40, 2000, B_TRUNCATE_BEGINNING), kLocationColumn);
	fList->SetTarget(this);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(-1)
		.Add(fList);
}


void StackWindow::Update(const BMessage &notice)
{
	fCurrent = notice.GetInt32("current", 0);
	fList->Clear();
	FrameRow *current = nullptr;
	const char *function;
	for (int32 i = 0; notice.FindString("function", i, &function) == B_OK; i++) {
		const std::string path = notice.GetString("path", i, "");
		const int32 line = notice.GetInt32("line", i, 0);
		std::string location;
		if (!path.empty()) {
			location = FileName(path) + ":" + std::to_string(line);
		}
		FrameRow *row = new FrameRow(i, function, FileName(notice.GetString("module", i, "")), location);
		fList->AddRow(row);
		if (i == fCurrent) {
			current = row;
		}
	}
	if (current != nullptr) {
		fList->AddToSelection(current);
		fList->ScrollTo(current);
	}
}


void StackWindow::SelectFrame(bool showSource)
{
	const FrameRow *row = static_cast<FrameRow*>(fList->CurrentSelection());
	// Selection by Update() names the current frame already.
	if (row == nullptr || (row->index == fCurrent && !showSource)) {
		return;
	}
	fCurrent = row->index;
	BMessage select(kMsgSelectFrame);
	select.AddInt32("frame", row->index);
	select.AddBool("show", showSource);
	Session().SendMessage(&select);
}


void StackWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kNoticeStack:
			Update(*message);
			break;
		case kMsgFrameSelected:
			SelectFrame(false);
			break;
		case kMsgFrameInvoked:
			SelectFrame(true);
			break;
		case kNoticeState:
		case kNoticeOutput:
		case kNoticeThreads:
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
