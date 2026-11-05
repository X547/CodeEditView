#include <compat/sys/stat.h>
#include <stdio.h>
#include <sys/stat.h>

#include <exception>
#include <memory>
#include <string>

#include <Application.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Path.h>

#include "DiffView.h"
#include "DiffWindow.h"
#include "Git.h"
#include "Messages.h"
#include "Repository.h"


namespace GitDiff {


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditGitDiff";


// Accepts directories only in the open panel.
class DirectoryFilter: public BRefFilter {
public:
	bool Filter(const entry_ref *ref, BNode *node, struct stat_beos *stat, const char *mimeType) override
	{
		(void)ref;
		(void)node;
		(void)mimeType;
		return S_ISDIR(stat->st_mode);
	}
};


class GitDiffApp: public BApplication {
private:
	GitLibrary fGitLibrary;
	std::unique_ptr<BFilePanel> fOpenPanel;
	DirectoryFilter fDirectoryFilter;
	int32 fWindowCount {};
	bool fHadWindow {};

	void Open(const char *path);
	void ShowOpenPanel();

public:
	GitDiffApp();

	void ArgvReceived(int32 argc, char **argv) final;
	void RefsReceived(BMessage *message) final;
	void ReadyToRun() final;
	void MessageReceived(BMessage *message) final;
};


GitDiffApp::GitDiffApp():
	BApplication(kAppSignature)
{
}


// Opens the repository containing path, showing path if it is a file.
void GitDiffApp::Open(const char *path)
{
	fHadWindow = true;
	BPath normalized(path, nullptr, true);
	const std::string fullPath = normalized.InitCheck() == B_OK ? normalized.Path() : path;
	std::unique_ptr<Repository> repository;
	try {
		repository = std::make_unique<Repository>(fullPath);
	} catch (const std::exception &e) {
		ShowError(e.what());
		if (fWindowCount == 0) {
			ShowOpenPanel();
		}
		return;
	}
	const std::string relativePath = repository->RelativePath(fullPath).value_or(std::string());
	(new DiffWindow(std::move(repository), relativePath))->Show();
	fWindowCount++;
}


void GitDiffApp::ShowOpenPanel()
{
	if (fOpenPanel == nullptr) {
		fOpenPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL, nullptr, nullptr, B_DIRECTORY_NODE, false,
			nullptr, &fDirectoryFilter);
		fOpenPanel->Window()->SetTitle("Open repository");
	}
	fOpenPanel->Show();
}


void GitDiffApp::ArgvReceived(int32 argc, char **argv)
{
	const char *cwd = CurrentMessage()->GetString("cwd", nullptr);
	for (int32 i = 1; i < argc; i++) {
		BPath path;
		if (argv[i][0] == '/' || cwd == nullptr) {
			path.SetTo(argv[i]);
		} else {
			path.SetTo(cwd, argv[i]);
		}
		Open(path.Path());
	}
}


void GitDiffApp::RefsReceived(BMessage *message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		Open(BPath(&ref).Path());
	}
}


void GitDiffApp::ReadyToRun()
{
	if (!fHadWindow) {
		ShowOpenPanel();
	}
}


void GitDiffApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgOpenRepository:
			ShowOpenPanel();
			break;
		case kMsgWindowClosed:
			fWindowCount--;
			if (fWindowCount == 0) {
				PostMessage(B_QUIT_REQUESTED);
			}
			break;
		case B_CANCEL:
			if (fWindowCount == 0) {
				PostMessage(B_QUIT_REQUESTED);
			}
			break;
		default:
			BApplication::MessageReceived(message);
			break;
	}
}


} // GitDiff


int main()
{
	GitDiff::GitDiffApp app;
	app.Run();
	return 0;
}
