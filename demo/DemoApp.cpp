#include <stdio.h>

#include <exception>
#include <memory>

#include <Application.h>
#include <Entry.h>
#include <File.h>
#include <FilePanel.h>
#include <Path.h>

#include <CodeEditView/Language.h>
#include <CodeEditView/TextModel.h>

#include "DemoWindow.h"


static constexpr const char *kAppSignature = "application/x-vnd.CodeEditDemo";


class DemoApp: public BApplication {
private:
	std::unique_ptr<BFilePanel> fOpenPanel;
	int32 fWindowCount {};
	bool fHadWindow {};

	void NewDocument();
	void OpenDocument(const entry_ref &ref);

public:
	DemoApp();

	void ArgvReceived(int32 argc, char **argv) final;
	void RefsReceived(BMessage *message) final;
	void ReadyToRun() final;
	void MessageReceived(BMessage *message) final;
};


DemoApp::DemoApp():
	BApplication(kAppSignature)
{
}


void DemoApp::NewDocument()
{
	BReference<DemoDocument> document(new DemoDocument(), true);
	(new DemoWindow(document.Get()))->Show();
	fHadWindow = true;
}


void DemoApp::OpenDocument(const entry_ref &ref)
{
	BReference<DemoDocument> document(new DemoDocument(), true);
	document->path.SetTo(&ref);

	BFile file(&ref, B_READ_ONLY);
	try {
		CodeEdit::ModelLock lock(*document->model);
		if (file.InitCheck() == B_OK) {
			document->model->Load(file);
		}
		document->model->SetLanguage(CodeEdit::LanguageForPath(ref.name));
	} catch (const std::exception &e) {
		ShowError(e.what());
		return;
	}
	(new DemoWindow(document.Get()))->Show();
	fHadWindow = true;
}


void DemoApp::ArgvReceived(int32 argc, char **argv)
{
	const char *cwd = CurrentMessage()->GetString("cwd", nullptr);
	for (int32 i = 1; i < argc; i++) {
		BPath path;
		if (argv[i][0] == '/' || cwd == nullptr) {
			path.SetTo(argv[i]);
		} else {
			path.SetTo(cwd, argv[i]);
		}
		entry_ref ref;
		if (get_ref_for_path(path.Path(), &ref) != B_OK) {
			fprintf(stderr, "CodeEditDemo: cannot open %s\n", argv[i]);
			continue;
		}
		OpenDocument(ref);
	}
}


void DemoApp::RefsReceived(BMessage *message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		OpenDocument(ref);
	}
}


void DemoApp::ReadyToRun()
{
	if (!fHadWindow) {
		NewDocument();
	}
}


void DemoApp::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgNewDocument:
			NewDocument();
			break;
		case kMsgOpenDocument:
			if (fOpenPanel == nullptr) {
				fOpenPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL);
			}
			fOpenPanel->Show();
			break;
		case kMsgWindowOpened:
			fWindowCount++;
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


int main()
{
	DemoApp app;
	app.Run();
	return 0;
}
