#include "NetworkReporter.h"

#include <Alert.h>

#include "Dialogs.h"
#include "Messages.h"
#include "Progress.h"


namespace GitTool {


static constexpr bigtime_t kProgressInterval = 100000;


NetworkReporter::NetworkReporter(DiffKit::Progress &progress, const BMessenger &logTarget):
	fProgress(progress),
	fLogTarget(logTarget)
{
}


bool NetworkReporter::IsCancelled()
{
	return fProgress.Cancelled();
}


void NetworkReporter::Progress(const std::string &text)
{
	const bigtime_t now = system_time();
	if (now - fLastProgress < kProgressInterval) {
		return;
	}
	fLastProgress = now;
	fProgress.SetText(text);
}


void NetworkReporter::Log(const std::string &text)
{
	BMessage message(kMsgNetworkLog);
	message.AddString("text", text.c_str());
	fLogTarget.SendMessage(&message);
}


bool NetworkReporter::AskPassword(const std::string &url, std::string &user, std::string &password)
{
	Form form("Sign in", "Sign in");
	form.AddLabel(url)
		.AddText("user", "User name:", user)
		.AddPassword("password", "Password:");
	if (!form.Go()) {
		return false;
	}
	user = form.Text("user");
	password = form.Text("password");
	return true;
}


bool NetworkReporter::ConfirmServer(const std::string &host, const std::string &details)
{
	const std::string text = "Connect to " + host + "?\n\n" + details;
	BAlert *alert = new BAlert("Unknown server", text.c_str(), "Cancel", "Connect", nullptr, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go() == 1;
}


} // GitTool
