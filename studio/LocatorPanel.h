#pragma once

#include <memory>
#include <optional>
#include <string>

#include <Messenger.h>
#include <Window.h>

#include "Locator.h"


class BButton;
class BGroupLayout;
class BMenuField;
class BPopUpMenu;
class BStringView;


namespace Studio {


class EntryListView;
struct ListingSlot;


// Browses the folders of any provider as Tracker's open panel browses those
// on disk: the folder with its parents as a menu field, the entries as a
// table with icons, sizes, times and kinds. Sends kMsgOpenLocator with each
// file chosen to the target, then closes. Folders are listed by a job off the
// window's thread; the window does not wait for it. Subclasses add the
// controls that choose the place (a repository and a commit, a server) to
// the header and start with Navigate().
class LocatorPanel: public BWindow {
private:
	BMessenger fTarget;
	BGroupLayout *fHeader;
	BPopUpMenu *fFolderMenu;
	BMenuField *fFolderField;
	BButton *fUpButton;
	EntryListView *fList;
	BStringView *fStatus;
	BButton *fOpenButton;
	// The folder shown, resolved.
	std::optional<Workspace::Locator> fFolder;
	std::shared_ptr<ListingSlot> fSlot;
	int32 fGeneration {};

	void ListingDone();
	void ShowFolder(const Workspace::Locator &folder);
	void GoUp();
	void Invoke();
	void Open(const Workspace::Locator &file);
	void UpdateButtons();

protected:
	// Where subclasses add their controls, above the folder.
	BGroupLayout *HeaderLayout() const {return fHeader;}
	void SetStatus(const std::string &text);
	// Lists the folder (resolved first, as a Git revision name is) and
	// shows it with the entry named select selected (else the first).
	void Navigate(const Workspace::Locator &folder, const std::string &select = {});

public:
	LocatorPanel(const char *title, const BMessenger &target);

	void MessageReceived(BMessage *message) override;
};


} // Studio
