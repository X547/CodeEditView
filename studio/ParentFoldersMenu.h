#pragma once

#include <stddef.h>

#include <string>
#include <vector>

#include <Entry.h>
#include <Messenger.h>

#include "LazyMenu.h"
#include "Locator.h"
#include "Provider.h"


class BBitmap;
class BPopUpMenu;


namespace Studio {


// Where the items of the folder menus send B_REFS_RECEIVED: folders and the
// files CodeEdit does not edit to Tracker, which opens them as from its own
// menus; text files to the editor window.
struct FolderMenuTargets {
	BMessenger editor;
	BMessenger tracker;
};


// An item with an icon before its label: the Tracker icon of a file or
// folder on disk, or the icon of a MIME type (for entries not on disk).
class EntryItem: public BMenuItem {
private:
	BBitmap *fIcon {};

protected:
	void GetContentSize(float *width, float *height) override;
	void DrawContent() override;

public:
	EntryItem(const char *label, const entry_ref &ref, BMessage *message);
	EntryItem(BMenu *submenu, const entry_ref &ref, BMessage *message);
	EntryItem(const char *label, const char *type, BMessage *message);
	EntryItem(BMenu *submenu, const char *type, BMessage *message);
	~EntryItem() override;
};


// The contents of a folder, read each time it opens (in the menu's thread):
// folders first, each with its own contents as a submenu, then files.
class FolderMenu: public LazyMenu {
private:
	struct Item {
		// A link is followed, so a linked folder lists the folder.
		entry_ref ref;
		std::string name;
		bool folder {};
	};

	entry_ref fFolder;
	FolderMenuTargets fTargets;
	std::vector<Item> fItems;

protected:
	size_t Prepare() final;
	BMenuItem *MakeItem(size_t index) final;
	void Release() final;

public:
	FolderMenu(const char *name, const entry_ref &folder, const FolderMenuTargets &targets);
};


// The folders of a file as Tracker, StyledEdit and Pe show them when the
// field beside the horizontal scroll bar is clicked (BDirMenu in reverse
// order): the file's own folder at the top, next to the field, up to the
// root of its volume at the bottom. Choosing a folder
// opens it in Tracker; its submenu is a FolderMenu. Text files chosen go to
// the editor. nullptr if the file is not on disk.
BPopUpMenu *CreateParentFoldersMenu(const std::string &path, const BMessenger &editor);


// The contents of a directory of a Git commit, read each time it opens (in
// the menu's thread; the repository is local): directories first, each with
// its own contents as a submenu, then files. Choosing a file sends
// kMsgOpenLocator to the editor; choosing a directory sends it
// kMsgShowOpenGit with the directory.
class GitFolderMenu: public LazyMenu {
private:
	struct Item {
		std::string name;
		// The MIME type of a file, guessed from its name.
		std::string type;
		bool folder {};
	};

	Workspace::Locator fFolder;
	BMessenger fEditor;
	std::vector<Item> fItems;
	// Why the directory could not be read.
	std::string fError;

protected:
	size_t Prepare() final;
	BMenuItem *MakeItem(size_t index) final;
	void Release() final;

public:
	GitFolderMenu(const char *name, const Workspace::Locator &folder, const BMessenger &editor);
};


// The directories of a file of a Git commit, in the order of
// CreateParentFoldersMenu(): the file's own directory at the top down to the
// root of the commit's tree ("repo @ 1a2b3c4") at the bottom, each a
// GitFolderMenu. Nothing is read from the repository until a submenu opens.
// nullptr if the locator is not a file of a commit.
BPopUpMenu *CreateGitParentFoldersMenu(const Workspace::Locator &file, const BMessenger &editor);


} // Studio
