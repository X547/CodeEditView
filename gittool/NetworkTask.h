#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include <Messenger.h>
#include <OS.h>

#include "Network.h"


namespace GitTool {


// Runs network work on its own thread with its own repository handle, and
// reports to a window: kMsgNetworkProgress, kMsgNetworkLog, and finally the
// done message with "error" added.
class NetworkTask: public NetworkHandler {
public:
	// Report lines are shown to the user when the work finishes.
	using Work = std::function<void(git_repository *repository, NetworkHandler &handler,
		std::vector<std::string> &report)>;

private:
	BMessenger fTarget;
	std::string fRepositoryPath;
	Work fWork;
	BMessage fDone;
	std::vector<std::string> fReport;
	std::atomic<bool> fCancelled {};
	thread_id fThread = -1;
	bigtime_t fLastProgress {};

	static status_t ThreadEntry(void *data);
	void Run();

public:
	NetworkTask(const BMessenger &target, const std::string &repositoryPath, Work work, const BMessage &done);
	// Waits for the thread.
	~NetworkTask() override;

	void Start();
	void Cancel() {fCancelled = true;}
	// Valid after the done message.
	const std::vector<std::string> &Report() const {return fReport;}

	bool IsCancelled() final {return fCancelled;}
	void Progress(const std::string &text) final;
	void Log(const std::string &text) final;
	bool AskPassword(const std::string &url, std::string &user, std::string &password) final;
	bool ConfirmServer(const std::string &host, const std::string &details) final;
};


} // GitTool
