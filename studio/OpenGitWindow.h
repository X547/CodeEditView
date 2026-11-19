#pragma once

#include <optional>
#include <string>
#include <vector>

#include <Messenger.h>
#include <Window.h>

#include "Locator.h"
#include "Provider.h"


class BFilePanel;
class BListView;
class BStringView;
class BTextControl;


namespace Studio {


// Browses the tree of a Git commit and sends kMsgOpenLocator with the chosen
// file to the target. The repository is read in the window's thread (local
// and fast).
class OpenGitWindow: public BWindow {
private:
	BMessenger fTarget;
	BTextControl *fRepository;
	BTextControl *fRevision;
	BStringView *fPlace;
	BListView *fList;
	BStringView *fStatus;
	BFilePanel *fDirectoryPanel {};
	// The directory shown, with the resolved commit.
	std::optional<Workspace::Locator> fDirectory;
	std::vector<Workspace::DirEntry> fEntries;
	// The directory of the tree shown first; then the root.
	std::string fStartPath;

	void ShowCommit();
	void ShowDirectory(const Workspace::Locator &directory);
	void Invoke();
	void SetStatus(const std::string &text);

public:
	// The commit's tree is shown at once if the repository directory is
	// given, at path (relative, empty for the root).
	OpenGitWindow(
		const BMessenger &target,
		const std::string &directory,
		const std::string &revision = "HEAD",
		const std::string &path = {}
	);
	~OpenGitWindow() override;

	void MessageReceived(BMessage *message) override;
};


} // Studio
