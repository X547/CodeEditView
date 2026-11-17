#include "ParentFoldersMenu.h"

#include <math.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
#include <iterator>

#include <Bitmap.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Node.h>
#include <NodeInfo.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Volume.h>

#include "NameOrder.h"


namespace Studio {


static constexpr const char *kTrackerSignature = "application/x-vnd.Be-TRAK";


// The bounds of an icon: B_MINI_ICON scaled with the font.
static BRect IconBounds()
{
	return BRect(BPoint(0, 0), be_control_look->ComposeIconSize(B_MINI_ICON));
}


//#pragma mark - EntryItem

EntryItem::EntryItem(const char *label, const entry_ref &ref, BMessage *message):
	BMenuItem(label, message)
{
	LoadIcon(ref);
}


EntryItem::EntryItem(BMenu *submenu, const entry_ref &ref, BMessage *message):
	BMenuItem(submenu, message)
{
	LoadIcon(ref);
}


EntryItem::~EntryItem()
{
	delete fIcon;
}


void EntryItem::LoadIcon(const entry_ref &ref)
{
	const BRect bounds = IconBounds();
	fIcon = new BBitmap(bounds, B_RGBA32);
	const icon_size size = static_cast<icon_size>(bounds.IntegerWidth() + 1);
	if (fIcon->InitCheck() != B_OK || BNodeInfo::GetTrackerIcon(&ref, fIcon, size) != B_OK) {
		delete fIcon;
		fIcon = nullptr;
	}
}


// The place of the icon is kept without one, so the labels line up.
void EntryItem::GetContentSize(float *width, float *height)
{
	BMenuItem::GetContentSize(width, height);
	const BRect icon = IconBounds();
	*width += icon.Width() + 1 + be_control_look->DefaultLabelSpacing();
	*height = std::max(*height, icon.Height() + 1);
}


void EntryItem::DrawContent()
{
	BMenu *menu = Menu();
	const BRect frame = Frame();
	const BRect icon = IconBounds();
	BPoint location = ContentLocation();
	if (fIcon != nullptr) {
		menu->PushState();
		menu->SetDrawingMode(B_OP_ALPHA);
		menu->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		menu->DrawBitmap(fIcon, BPoint(location.x, floorf(frame.top + (frame.Height() - icon.Height()) / 2)));
		menu->PopState();
	}
	// The label is drawn from the pen location, centered beside the icon.
	font_height fontHeight;
	menu->GetFontHeight(&fontHeight);
	const float textHeight = ceilf(fontHeight.ascent + fontHeight.descent);
	if (icon.Height() + 1 > textHeight) {
		location.y += floorf((icon.Height() + 1 - textHeight) / 2);
	}
	location.x += icon.Width() + 1 + be_control_look->DefaultLabelSpacing();
	menu->MovePenTo(location);
	BMenuItem::DrawContent();
}


//#pragma mark - FolderMenu

// The types CodeEdit opens (as its file_types resource), and files without
// a known type.
static bool IsText(const entry_ref &ref)
{
	BNode node(&ref);
	if (node.InitCheck() != B_OK) {
		return false;
	}
	BNodeInfo info(&node);
	char type[B_MIME_TYPE_LENGTH] = {};
	if (info.InitCheck() != B_OK || info.GetType(type) != B_OK || type[0] == '\0') {
		return true;
	}
	static constexpr const char *kTypes[] = {
		"application/octet-stream",
		"application/json",
		"application/x-shellscript",
	};
	if (strncasecmp(type, "text/", 5) == 0 || strcasecmp(type, "text") == 0) {
		return true;
	}
	return std::any_of(std::begin(kTypes), std::end(kTypes), [&type](const char *known) {
		return strcasecmp(type, known) == 0;
	});
}


static BMessage *RefsMessage(const entry_ref &ref)
{
	BMessage *message = new BMessage(B_REFS_RECEIVED);
	message->AddRef("refs", &ref);
	return message;
}


FolderMenu::FolderMenu(const char *name, const entry_ref &folder, const FolderMenuTargets &targets):
	LazyMenu(name),
	fFolder(folder),
	fTargets(targets)
{
}


size_t FolderMenu::Prepare()
{
	fItems.clear();
	BDirectory directory(&fFolder);
	entry_ref ref;
	while (directory.GetNextRef(&ref) == B_OK) {
		const BEntry entry(&ref, true);
		entry_ref target;
		// A broken link is shown as a file.
		if (entry.InitCheck() != B_OK || entry.GetRef(&target) != B_OK) {
			target = ref;
		}
		fItems.push_back({target, ref.name, entry.InitCheck() == B_OK && entry.IsDirectory()});
	}
	std::sort(fItems.begin(), fItems.end(), [](const Item &a, const Item &b) {
		if (a.folder != b.folder) {
			return a.folder;
		}
		return DiffKit::NameLess(a.name, b.name);
	});
	return std::max<size_t>(fItems.size(), 1);
}


BMenuItem *FolderMenu::MakeItem(size_t index)
{
	if (fItems.empty()) {
		BMenuItem *empty = new BMenuItem("Empty folder", nullptr);
		empty->SetEnabled(false);
		return empty;
	}
	const Item &item = fItems[index];
	if (item.folder) {
		EntryItem *folder = new EntryItem(
			new FolderMenu(item.name.c_str(), item.ref, fTargets),
			item.ref,
			RefsMessage(item.ref)
		);
		folder->SetTarget(fTargets.tracker);
		return folder;
	}
	EntryItem *file = new EntryItem(item.name.c_str(), item.ref, RefsMessage(item.ref));
	file->SetTarget(IsText(item.ref) ? fTargets.editor : fTargets.tracker);
	return file;
}


void FolderMenu::Release()
{
	fItems = std::vector<Item>();
}


//#pragma mark - CreateParentFoldersMenu

// A volume's root is named as the volume, as Tracker names it.
static std::string FolderLabel(const entry_ref &ref)
{
	BDirectory directory(&ref);
	if (directory.InitCheck() == B_OK && directory.IsRootDirectory()) {
		const BPath path(&ref);
		if (path.InitCheck() == B_OK && strcmp(path.Path(), "/") == 0) {
			return "/";
		}
		// The ref of a mount point is on the volume above; its node is the
		// root of the mounted one.
		node_ref node;
		char name[B_FILE_NAME_LENGTH];
		if (directory.GetNodeRef(&node) == B_OK && BVolume(node.device).GetName(name) == B_OK) {
			return name;
		}
	}
	return ref.name;
}


BPopUpMenu *CreateParentFoldersMenu(const std::string &path, const BMessenger &editor)
{
	BEntry entry(path.c_str());
	if (entry.InitCheck() != B_OK || !entry.Exists()) {
		return nullptr;
	}
	// From the file's folder up to the root of its volume.
	std::vector<entry_ref> folders;
	BEntry parent;
	while (entry.GetParent(&parent) == B_OK) {
		entry_ref ref;
		if (parent.GetRef(&ref) != B_OK) {
			break;
		}
		folders.push_back(ref);
		if (BDirectory(&parent).IsRootDirectory()) {
			break;
		}
		entry = parent;
	}
	if (folders.empty()) {
		return nullptr;
	}

	const FolderMenuTargets targets {editor, BMessenger(kTrackerSignature)};
	BPopUpMenu *menu = new BPopUpMenu("parentFolders", false, false);
	for (auto folder = folders.rbegin(); folder != folders.rend(); folder++) {
		const std::string label = FolderLabel(*folder);
		EntryItem *item = new EntryItem(
			new FolderMenu(label.c_str(), *folder, targets),
			*folder,
			RefsMessage(*folder)
		);
		item->SetTarget(targets.tracker);
		menu->AddItem(item);
	}
	menu->SetAsyncAutoDestruct(true);
	return menu;
}


} // Studio
