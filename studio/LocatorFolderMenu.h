#pragma once

#include <stddef.h>

#include <string>
#include <vector>

#include <Messenger.h>

#include "LazyMenu.h"
#include "Locator.h"


class BPopUpMenu;


namespace Studio {


// What the items of the locator folder menus send to their target, each
// with "locator" (string): the folder or file chosen.
struct LocatorMenuMessages {
	uint32 folder {};
	uint32 file {};
};


// The contents of a folder of any provider, read each time it opens (in the
// menu's thread, so only for providers that answer quickly, as a local Git
// repository does): folders first, each with its own contents as a submenu,
// then files, with the icons of their types.
class LocatorFolderMenu: public LazyMenu {
private:
	struct Item {
		std::string name;
		std::string type;
		bool folder {};
	};

	Workspace::Locator fFolder;
	BMessenger fTarget;
	LocatorMenuMessages fMessages;
	std::vector<Item> fItems;
	// Why the folder could not be read.
	std::string fError;

protected:
	size_t Prepare() final;
	BMenuItem *MakeItem(size_t index) final;
	void Release() final;

public:
	LocatorFolderMenu(
		const char *name,
		const Workspace::Locator &folder,
		const BMessenger &target,
		const LocatorMenuMessages &messages
	);
};


// The name a folder is shown with: its last component; the root of a Git
// commit's tree as the commit ("repo @ 1a2b3c4"); "/" for the root.
std::string LocatorFolderLabel(const Workspace::Locator &folder);

// The locator of an entry of a folder.
Workspace::Locator ChildLocator(const Workspace::Locator &folder, const std::string &name);

// Adds the folder and its parents up to the root (the folder at the top), as
// BDirMenu in reverse order; each item has a LocatorFolderMenu.
void AddLocatorFolderItems(
	BMenu *menu,
	const Workspace::Locator &folder,
	const BMessenger &target,
	const LocatorMenuMessages &messages
);

// The folders of a file as CreateParentFoldersMenu() shows those on disk.
// Nothing is listed until a submenu opens. nullptr for a root or an untitled
// document.
BPopUpMenu *CreateLocatorParentFoldersMenu(
	const Workspace::Locator &file,
	const BMessenger &target,
	const LocatorMenuMessages &messages
);


} // Studio
