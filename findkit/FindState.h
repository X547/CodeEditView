#pragma once

#include <string>

#include <Messenger.h>
#include <SupportDefs.h>


namespace FindKit {


// Sent to the watcher when the find settings changed.
constexpr uint32 kMsgFindSettingsChanged = 'FKsc';
// Sent to the watcher with the result of a find command: "status" (string),
// "error" (bool).
constexpr uint32 kMsgFindStatus = 'FKst';


struct FindSettings {
	// LF line endings.
	std::string findText;
	std::string replaceText;
	bool regex {};
	bool ignoreCase {};
	bool wrap = true;
	bool backward {};
	bool inSelection {};

	bool operator==(const FindSettings &other) const = default;
};


// Find settings shared by all windows of the application.
namespace FindState {


FindSettings Get();
// The watcher is notified of changes unless it made them itself.
void Set(const FindSettings &settings, bool notifyWatcher = true);

// The find panel; receives the messages above.
void SetWatcher(const BMessenger &watcher);
void ReportStatus(const std::string &status, bool error);


} // FindState


} // FindKit
