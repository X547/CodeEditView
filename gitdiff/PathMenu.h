#pragma once

#include <memory>
#include <string>

#include <Menu.h>
#include <Message.h>
#include <Messenger.h>

#include "PathSource.h"


namespace GitDiff {


// A Tracker-like cascade of folders and files. A folder's submenu is filled
// each time it opens; choosing a file sends a copy of the message with the
// relative path in "path" to the target.
class PathMenu: public BMenu {
private:
	std::shared_ptr<const PathSource> fSource;
	std::string fFolder;
	BMessage fMessage;
	BMessenger fTarget;

	void Fill();

public:
	PathMenu(const char *label, std::shared_ptr<const PathSource> source, const std::string &folder,
		const BMessage &message, const BMessenger &target);

	bool AddDynamicItem(add_state state) override;
};


} // GitDiff
