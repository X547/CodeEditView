#include "CodeEditLink.h"

#include <string.h>

#include <string>
#include <thread>

#include <Alert.h>
#include <Application.h>
#include <Entry.h>
#include <Message.h>
#include <Messenger.h>
#include <Path.h>
#include <Roster.h>


namespace DiffKit {


static bool Launched(status_t status)
{
	return status == B_OK || status == B_ALREADY_RUNNING;
}


// Launching blocks while the executable loads: not on a window thread.
static status_t Deliver(BMessage &message)
{
	const BMessenger running(kCodeEditSignature);
	if (running.IsValid()) {
		return running.SendMessage(&message);
	}
	status_t status = be_roster->Launch(kCodeEditSignature, &message);
	if (Launched(status)) {
		return B_OK;
	}

	// Not installed: a CodeEdit built with this program.
	app_info info;
	BPath directory;
	if (be_app == nullptr || be_app->GetAppInfo(&info) != B_OK || BPath(&info.ref).GetParent(&directory) != B_OK) {
		return status;
	}
	for (const char *relative : {"CodeEdit", "../studio/CodeEdit"}) {
		const BPath candidate(directory.Path(), relative, true);
		BEntry entry(candidate.Path());
		entry_ref ref;
		if (candidate.InitCheck() != B_OK || !entry.Exists() || entry.GetRef(&ref) != B_OK) {
			continue;
		}
		status = be_roster->Launch(&ref, &message);
		if (Launched(status)) {
			return B_OK;
		}
	}
	return status;
}


void SendToCodeEdit(const BMessage &message)
{
	std::thread([message = BMessage(message)]() mutable {
		const status_t status = Deliver(message);
		if (status != B_OK) {
			const std::string text = std::string("Cannot open CodeEdit: ") + strerror(status);
			(new BAlert("CodeEdit", text.c_str(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(nullptr);
		}
	}).detach();
}


void OpenFileInCodeEdit(const char *path, int32 line, int32 column)
{
	entry_ref ref;
	if (get_ref_for_path(path, &ref) != B_OK) {
		return;
	}
	BMessage message(B_REFS_RECEIVED);
	message.AddRef("refs", &ref);
	if (line > 0) {
		message.AddInt32("be:line", line);
		if (column > 0) {
			message.AddInt32("be:column", column);
		}
	}
	SendToCodeEdit(message);
}


} // DiffKit
