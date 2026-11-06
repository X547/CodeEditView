#include "FindState.h"

#include <Autolock.h>
#include <Locker.h>
#include <Message.h>


namespace FindKit {


namespace FindState {


static BLocker sLock("find state");
static FindSettings sSettings;
static BMessenger sWatcher;


FindSettings Get()
{
	BAutolock lock(sLock);
	return sSettings;
}


void Set(const FindSettings &settings)
{
	BMessenger watcher;
	{
		BAutolock lock(sLock);
		if (settings == sSettings) {
			return;
		}
		sSettings = settings;
		watcher = sWatcher;
	}
	watcher.SendMessage(kMsgFindSettingsChanged);
}


void SetWatcher(const BMessenger &watcher)
{
	BAutolock lock(sLock);
	sWatcher = watcher;
}


void ReportStatus(const std::string &status, bool error)
{
	BMessenger watcher;
	{
		BAutolock lock(sLock);
		watcher = sWatcher;
	}
	BMessage message(kMsgFindStatus);
	message.AddString("status", status.c_str());
	message.AddBool("error", error);
	watcher.SendMessage(&message);
}


} // FindState


} // FindKit
