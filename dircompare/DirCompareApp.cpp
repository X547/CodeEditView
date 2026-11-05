#include <string>
#include <vector>

#include <Application.h>
#include <Entry.h>
#include <Path.h>

#include <DiffView.h>
#include <Git.h>

#include "Compare.h"
#include "CompareWindow.h"
#include "Messages.h"
#include "OpenWindow.h"


namespace DirCompare {


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditDirCompare";


class DirCompareApp: public BApplication {
private:
	// The diff of two texts uses libgit2.
	DiffKit::GitLibrary fGitLibrary;
	int32 fWindowCount {};

	void Open(const std::vector<std::string> &paths);
	void ShowOpenWindow(const std::string &left = std::string(), const std::string &right = std::string());

public:
	DirCompareApp();

	void ArgvReceived(int32 argc, char **argv) final;
	void RefsReceived(BMessage *message) final;
	void ReadyToRun() final;
	void MessageReceived(BMessage *message) final;
};


DirCompareApp::DirCompareApp():
	BApplication(kAppSignature)
{
}


void DirCompareApp::ShowOpenWindow(const std::string &left, const std::string &right)
{
	(new OpenWindow(left, right))->Show();
	fWindowCount++;
}


// Compares two directories or two files; anything else opens the open
// window with what was given.
void DirCompareApp::Open(const std::vector<std::string> &paths)
{
	if (paths.size() != 2) {
		ShowOpenWindow(paths.empty() ? std::string() : paths[0]);
		return;
	}
	const EntryType types[2] = {ReadEntryInfo(paths[0]).type, ReadEntryInfo(paths[1]).type};
	const char *error = nullptr;
	if (types[0] == EntryType::None || types[1] == EntryType::None) {
		error = "Both paths must exist.";
	} else if ((types[0] == EntryType::Directory) != (types[1] == EntryType::Directory)) {
		error = "Compare two directories or two files.";
	}
	if (error != nullptr) {
		DiffKit::ShowError(error);
		ShowOpenWindow(paths[0], paths[1]);
		return;
	}
	(new CompareWindow(paths[0], paths[1]))->Show();
	fWindowCount++;
}


void DirCompareApp::ArgvReceived(int32 argc, char **argv)
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
	Open(paths);
}


void DirCompareApp::RefsReceived(BMessage *message)
{
	std::vector<std::string> paths;
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		paths.push_back(BPath(&ref).Path());
	}
	Open(paths);
}


void DirCompareApp::ReadyToRun()
{
	if (fWindowCount == 0) {
		ShowOpenWindow();
	}
}


void DirCompareApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgNewComparison:
			ShowOpenWindow();
			break;
		case kMsgOpenComparison:
			Open({message->GetString("left", ""), message->GetString("right", "")});
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


} // DirCompare


int main()
{
	DirCompare::DirCompareApp app;
	app.Run();
	return 0;
}
