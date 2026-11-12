#include "ThreadsWindow.h"

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>

#include <string>


namespace Debugger {


static constexpr uint32 kMsgThreadSelected = 'TWse';
static constexpr uint32 kMsgThreadInvoked = 'TWiv';

enum {
	kIdColumn,
	kNameColumn,
};


class ThreadRow: public BRow {
public:
	const int64_t id;

	ThreadRow(int64_t id, const char *name):
		id(id)
	{
		SetField(new BStringField(std::to_string(id).c_str()), kIdColumn);
		SetField(new BStringField(name), kNameColumn);
	}
};


ThreadsWindow::ThreadsWindow(const SessionContext &context):
	ToolWindow(BRect(40, 360, 339, 509), "Threads", context),
	fList(CreateColumnList("threads", kMsgThreadSelected, kMsgThreadInvoked))
{
	fList->AddColumn(new BStringColumn("ID", 60, 30, 200, B_TRUNCATE_END, B_ALIGN_RIGHT), kIdColumn);
	fList->AddColumn(new BStringColumn("Name", 200, 50, 1000, B_TRUNCATE_END), kNameColumn);
	fList->SetTarget(this);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(-1)
		.Add(fList);
}


void ThreadsWindow::Update(const BMessage &notice)
{
	fCurrent = notice.GetInt64("current", 0);
	fList->Clear();
	ThreadRow *current = nullptr;
	int64 id;
	for (int32 i = 0; notice.FindInt64("id", i, &id) == B_OK; i++) {
		ThreadRow *row = new ThreadRow(id, notice.GetString("name", i, ""));
		fList->AddRow(row);
		if (id == fCurrent) {
			current = row;
		}
	}
	if (current != nullptr) {
		fList->AddToSelection(current);
		fList->ScrollTo(current);
	}
}


void ThreadsWindow::SessionChanged()
{
	fList->Clear();
	fCurrent = 0;
}


void ThreadsWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kNoticeThreads:
			Update(*message);
			break;
		case kMsgThreadSelected:
		case kMsgThreadInvoked: {
			// Selection by Update() names the current thread already.
			const ThreadRow *row = static_cast<ThreadRow*>(fList->CurrentSelection());
			if (row == nullptr || row->id == fCurrent) {
				break;
			}
			fCurrent = row->id;
			BMessage select(kMsgSelectThread);
			select.AddInt64("thread", row->id);
			Session().SendMessage(&select);
			break;
		}
		case kNoticeState:
		case kNoticeOutput:
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
