#pragma once

#include <string>

#include "LocatorPanel.h"


class BFilePanel;
class BTextControl;


namespace Studio {


// Browses the tree of a Git commit: a LocatorPanel with the repository and
// the revision above.
class OpenGitWindow: public LocatorPanel {
private:
	BTextControl *fRepository;
	BTextControl *fRevision;
	BFilePanel *fDirectoryPanel {};

	void ShowCommit(const std::string &path = {});

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
