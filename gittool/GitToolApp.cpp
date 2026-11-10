#include <compat/sys/stat.h>
#include <sys/stat.h>

#include <exception>
#include <memory>
#include <string>

#include <Application.h>
#include <Entry.h>
#include <FilePanel.h>
#include <Path.h>

#include "DiffView.h"
#include "FreezeWatchdog.h"
#include "Git.h"
#include "MainWindow.h"
#include "Messages.h"
#include "Snapshot.h"

// The messages of the Git diff windows shown by this application.
#include "../gitdiff/Messages.h"


namespace GitTool {


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditGitTool";


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


class GitToolApp: public BApplication {
private:
	GitLibrary fGitLibrary;
	std::unique_ptr<BFilePanel> fOpenPanel;
	DirectoryFilter fDirectoryFilter;
	int32 fWindowCount {};
	bool fHadWindow {};

	void Open(const char *path);
	void ShowOpenPanel();

public:
	GitToolApp();

	void ArgvReceived(int32 argc, char **argv) final;
	void RefsReceived(BMessage *message) final;
	void ReadyToRun() final;
	void MessageReceived(BMessage *message) final;
};


GitToolApp::GitToolApp():
	BApplication(kAppSignature)
{
	DiffKit::StartFreezeWatchdog("CodeEditGitTool");
}


// Opens the repository containing path, or shows its window if it is open.
void GitToolApp::Open(const char *path)
{
	fHadWindow = true;
	RepositoryPtr repository;
	try {
		repository = OpenRepository(path);
	} catch (const std::exception &e) {
		DiffKit::ShowError(e.what());
		if (fWindowCount == 0) {
			ShowOpenPanel();
		}
		return;
	}
	const std::string repositoryPath = git_repository_commondir(repository.get());
	for (int32 i = 0; BWindow *window = WindowAt(i); i++) {
		MainWindow *mainWindow = dynamic_cast<MainWindow*>(window);
		if (mainWindow != nullptr && mainWindow->RepositoryPath() == repositoryPath) {
			mainWindow->Activate();
			return;
		}
	}
	(new MainWindow(std::move(repository)))->Show();
	fWindowCount++;
}


void GitToolApp::ShowOpenPanel()
{
	if (fOpenPanel == nullptr) {
		fOpenPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL, nullptr, nullptr, B_DIRECTORY_NODE, false,
			nullptr, &fDirectoryFilter);
		fOpenPanel->Window()->SetTitle("Open repository");
	}
	fOpenPanel->Show();
}


void GitToolApp::ArgvReceived(int32 argc, char **argv)
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


void GitToolApp::RefsReceived(BMessage *message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		Open(BPath(&ref).Path());
	}
}


void GitToolApp::ReadyToRun()
{
	if (!fHadWindow) {
		ShowOpenPanel();
	}
}


void GitToolApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgOpenRepository:
		case GitDiff::kMsgOpenRepository:
			ShowOpenPanel();
			break;
		case kMsgMainWindowClosed:
			fWindowCount--;
			if (fWindowCount == 0) {
				PostMessage(B_QUIT_REQUESTED);
			}
			break;
		case GitDiff::kMsgWindowClosed:
			// Diff windows live as long as the application.
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


} // GitTool


int main()
{
	GitTool::GitToolApp app;
	app.Run();
	return 0;
}
