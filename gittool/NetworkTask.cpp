#include "NetworkTask.h"

#include <exception>

#include <Alert.h>

#include "Dialogs.h"
#include "Messages.h"


namespace GitTool {


static constexpr bigtime_t kProgressInterval = 100000;


NetworkTask::NetworkTask(const BMessenger &target, const std::string &repositoryPath, Work work, const BMessage &done):
	fTarget(target),
	fRepositoryPath(repositoryPath),
	fWork(std::move(work)),
	fDone(done)
{
}


NetworkTask::~NetworkTask()
{
	if (fThread >= 0) {
		fCancelled = true;
		status_t result;
		wait_for_thread(fThread, &result);
	}
}


void NetworkTask::Start()
{
	fThread = spawn_thread(ThreadEntry, "network", B_NORMAL_PRIORITY, this);
	resume_thread(fThread);
}


status_t NetworkTask::ThreadEntry(void *data)
{
	static_cast<NetworkTask*>(data)->Run();
	return B_OK;
}


void NetworkTask::Run()
{
	std::string error;
	try {
		RepositoryPtr repository = OpenRepository(fRepositoryPath);
		fWork(repository.get(), *this, fReport);
	} catch (const std::exception &e) {
		error = e.what();
	}
	BMessage done(fDone);
	done.AddString("error", error.c_str());
	fTarget.SendMessage(&done);
}


void NetworkTask::Progress(const std::string &text)
{
	const bigtime_t now = system_time();
	if (now - fLastProgress < kProgressInterval) {
		return;
	}
	fLastProgress = now;
	BMessage message(kMsgNetworkProgress);
	message.AddString("text", text.c_str());
	fTarget.SendMessage(&message);
}


void NetworkTask::Log(const std::string &text)
{
	BMessage message(kMsgNetworkLog);
	message.AddString("text", text.c_str());
	fTarget.SendMessage(&message);
}


bool NetworkTask::AskPassword(const std::string &url, std::string &user, std::string &password)
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


bool NetworkTask::ConfirmServer(const std::string &host, const std::string &details)
{
	const std::string text = "Connect to " + host + "?\n\n" + details;
	BAlert *alert = new BAlert("Unknown server", text.c_str(), "Cancel", "Connect", nullptr, B_WIDTH_AS_USUAL,
		B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go() == 1;
}


} // GitTool
