#include <string>
#include <vector>

#include <Alert.h>
#include <Application.h>
#include <Entry.h>
#include <Path.h>

#include "CoverageMessages.h"
#include "CoverageWindow.h"
#include "OpenWindow.h"


namespace Coverage {


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditCoverage";


static void ShowError(const char *message)
{
	BAlert *alert = new BAlert("CodeEditCoverage", message, "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
	alert->Go(nullptr);
}


class CoverageApp: public BApplication {
private:
	int32 fWindowCount {};

	void Open(const std::string &binary, const std::string &profile);
	void ShowOpenWindow(const std::string &binary = std::string(), const std::string &profile = std::string());

public:
	CoverageApp();

	void ArgvReceived(int32 argc, char **argv) final;
	void ReadyToRun() final;
	void MessageReceived(BMessage *message) final;
};


CoverageApp::CoverageApp():
	BApplication(kAppSignature)
{
}


void CoverageApp::ShowOpenWindow(const std::string &binary, const std::string &profile)
{
	(new OpenWindow(binary, profile))->Show();
	fWindowCount++;
}


void CoverageApp::Open(const std::string &binary, const std::string &profile)
{
	if (!BEntry(binary.c_str()).Exists() || !BEntry(profile.c_str()).Exists()) {
		ShowError("Choose an instrumented binary and its merged profile (.profdata).");
		ShowOpenWindow(binary, profile);
		return;
	}
	(new CoverageWindow(binary, profile))->Show();
	fWindowCount++;
}


// CodeEditCoverage <binary> <profile.profdata>
void CoverageApp::ArgvReceived(int32 argc, char **argv)
{
	const char *cwd = CurrentMessage()->GetString("cwd", nullptr);
	std::vector<std::string> paths;
	for (int32 i = 1; i < argc; i++) {
		BPath path;
		if (argv[i][0] == '/' || cwd == nullptr) {
			path.SetTo(argv[i]);
		} else {
			path.SetTo(cwd, argv[i]);
		}
		paths.push_back(path.Path() != nullptr ? path.Path() : argv[i]);
	}
	if (paths.size() == 2) {
		Open(paths[0], paths[1]);
	} else {
		ShowOpenWindow(paths.empty() ? std::string() : paths[0]);
	}
}


void CoverageApp::ReadyToRun()
{
	if (fWindowCount == 0) {
		ShowOpenWindow();
	}
}


void CoverageApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgShowOpenWindow:
			ShowOpenWindow();
			break;
		case kMsgOpenCoverage:
			Open(message->GetString("binary", ""), message->GetString("profile", ""));
			break;
		case kMsgWindowClosed:
			fWindowCount--;
			if (fWindowCount == 0) {
				PostMessage(B_QUIT_REQUESTED);
			}
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


} // Coverage


int main()
{
	Coverage::CoverageApp app;
	app.Run();
	return 0;
}
