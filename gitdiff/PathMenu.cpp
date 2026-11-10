#include "PathMenu.h"

#include <MenuItem.h>


namespace GitDiff {


//#pragma mark - PathMenu

PathMenu::PathMenu(const char *label, std::shared_ptr<const PathSource> source, const std::string &folder,
	const BMessage &message, const BMessenger &target):
	BMenu(label),
	fSource(std::move(source)),
	fFolder(folder),
	fMessage(message),
	fTarget(target)
{
}


// Filled each time it opens, so a folder on disk shows its current files.
void PathMenu::Fill()
{
	while (BMenuItem *item = RemoveItem(static_cast<int32>(0))) {
		delete item;
	}
	const PathSource::Entries entries = fSource->List(fFolder);
	const std::string prefix = fFolder.empty() ? "" : fFolder + "/";
	for (const std::string &name : entries.folders) {
		PathMenu *submenu = new PathMenu(name.c_str(), fSource, prefix + name, fMessage, fTarget);
		AddItem(new BMenuItem(submenu));
	}
	if (!entries.folders.empty() && !entries.files.empty()) {
		AddSeparatorItem();
	}
	for (const std::string &name : entries.files) {
		BMessage *message = new BMessage(fMessage);
		message->AddString("path", (prefix + name).c_str());
		BMenuItem *item = new BMenuItem(name.c_str(), message);
		item->SetTarget(fTarget);
		AddItem(item);
	}
	if (entries.folders.empty() && entries.files.empty()) {
		BMenuItem *item = new BMenuItem("(empty)", nullptr);
		item->SetEnabled(false);
		AddItem(item);
	}
}


bool PathMenu::AddDynamicItem(add_state state)
{
	if (state == B_INITIAL_ADD) {
		Fill();
	}
	return false;
}


} // GitDiff
