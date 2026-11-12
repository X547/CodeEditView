#pragma once

#include <string>

#include <Messenger.h>
#include <OS.h>

#include "Network.h"


namespace DiffKit {
class Progress;
}


namespace GitTool {


// Network work running under a progress dialog (DiffKit::RunWithProgress):
// progress and cancelling go through the dialog, log lines to a window as
// kMsgNetworkLog. Passwords and unknown servers are asked with dialogs that
// are waited for, as this runs on the work's thread.
class NetworkReporter: public NetworkHandler {
private:
	DiffKit::Progress &fProgress;
	BMessenger fLogTarget;
	bigtime_t fLastProgress {};

public:
	NetworkReporter(DiffKit::Progress &progress, const BMessenger &logTarget);

	bool IsCancelled() final;
	void Progress(const std::string &text) final;
	void Log(const std::string &text) final;
	bool AskPassword(const std::string &url, std::string &user, std::string &password) final;
	bool ConfirmServer(const std::string &host, const std::string &details) final;
};


} // GitTool
