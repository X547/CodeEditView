#include "LocatorFolderMenu.h"

#include <algorithm>

#include <PopUpMenu.h>

#include "EntryIcons.h"
#include "NameOrder.h"
#include "ParentFoldersMenu.h"
#include "Provider.h"


namespace Studio {


using Workspace::DirEntry;
using Workspace::Locator;
using Workspace::LocatorKind;
using Workspace::ProviderError;
using Workspace::Providers;


static BMessage *LocatorMessage(uint32 what, const Locator &locator)
{
	BMessage *message = new BMessage(what);
	message->AddString("locator", locator.ToString().c_str());
	return message;
}


Locator ChildLocator(const Locator &folder, const std::string &name)
{
	const std::string &path = folder.Path();
	if (path.empty()) {
		return folder.WithPath(name);
	}
	return folder.WithPath(path.back() == '/' ? path + name : path + "/" + name);
}


std::string LocatorFolderLabel(const Locator &folder)
{
	if (folder.Kind() == LocatorKind::GitCommit && folder.Path().empty()) {
		const std::string &revision = folder.Revision();
		const std::string commit = folder.IsResolved() ? revision.substr(0, 7) : revision;
		return Locator::File(folder.Repository()).Leaf() + " @ " + commit;
	}
	const std::string leaf = folder.Leaf();
	return leaf.empty() ? folder.Path() : leaf;
}


//#pragma mark - LocatorFolderMenu

LocatorFolderMenu::LocatorFolderMenu(
	const char *name,
	const Locator &folder,
	const BMessenger &target,
	const LocatorMenuMessages &messages
):
	LazyMenu(name),
	fFolder(folder),
	fTarget(target),
	fMessages(messages)
{
}


size_t LocatorFolderMenu::Prepare()
{
	fItems.clear();
	fError.clear();
	std::vector<DirEntry> entries;
	try {
		entries = Providers::Default().Get(fFolder).List(fFolder);
	} catch (const ProviderError &error) {
		fError = error.what();
		return 1;
	}
	for (const DirEntry &entry : entries) {
		fItems.push_back({entry.name, EntryType(entry), entry.directory});
	}
	std::sort(fItems.begin(), fItems.end(), [](const Item &a, const Item &b) {
		if (a.folder != b.folder) {
			return a.folder;
		}
		return DiffKit::NameLess(a.name, b.name);
	});
	return std::max<size_t>(fItems.size(), 1);
}


BMenuItem *LocatorFolderMenu::MakeItem(size_t index)
{
	if (!fError.empty() || fItems.empty()) {
		BMenuItem *none = new BMenuItem(fError.empty() ? "Empty folder" : fError.c_str(), nullptr);
		none->SetEnabled(false);
		return none;
	}
	const Item &item = fItems[index];
	const Locator locator = ChildLocator(fFolder, item.name);
	BMenuItem *result;
	if (item.folder) {
		result = new EntryItem(
			new LocatorFolderMenu(item.name.c_str(), locator, fTarget, fMessages),
			item.type.c_str(),
			LocatorMessage(fMessages.folder, locator)
		);
	} else {
		result = new EntryItem(item.name.c_str(), item.type.c_str(), LocatorMessage(fMessages.file, locator));
	}
	result->SetTarget(fTarget);
	return result;
}


void LocatorFolderMenu::Release()
{
	fItems = std::vector<Item>();
}


//#pragma mark - Folder lists

void AddLocatorFolderItems(
	BMenu *menu,
	const Locator &folder,
	const BMessenger &target,
	const LocatorMenuMessages &messages
)
{
	Locator each = folder;
	for (;;) {
		const std::string label = LocatorFolderLabel(each);
		EntryItem *item = new EntryItem(
			new LocatorFolderMenu(label.c_str(), each, target, messages),
			kDirectoryType,
			LocatorMessage(messages.folder, each)
		);
		item->SetTarget(target);
		menu->AddItem(item);
		const Locator parent = each.Parent();
		if (parent == each) {
			break;
		}
		each = parent;
	}
}


BPopUpMenu *CreateLocatorParentFoldersMenu(
	const Locator &file,
	const BMessenger &target,
	const LocatorMenuMessages &messages
)
{
	if (file.Kind() == LocatorKind::Untitled || file.Parent() == file) {
		return nullptr;
	}
	BPopUpMenu *menu = new BPopUpMenu("parentFolders", false, false);
	AddLocatorFolderItems(menu, file.Parent(), target, messages);
	menu->SetAsyncAutoDestruct(true);
	return menu;
}


} // Studio
