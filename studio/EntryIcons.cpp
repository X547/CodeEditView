#include "EntryIcons.h"

#include <Bitmap.h>
#include <ControlLook.h>
#include <Entry.h>
#include <Mime.h>
#include <NodeInfo.h>


namespace Studio {


BRect IconBounds()
{
	return BRect(BPoint(0, 0), be_control_look->ComposeIconSize(B_MINI_ICON));
}


// An icon drawn by get(bitmap, size); nullptr if it fails.
template<typename Get>
static BBitmap *NewIconBy(Get get)
{
	const BRect bounds = IconBounds();
	BBitmap *icon = new BBitmap(bounds, B_RGBA32);
	const icon_size size = static_cast<icon_size>(bounds.IntegerWidth() + 1);
	if (icon->InitCheck() != B_OK || get(icon, size) != B_OK) {
		delete icon;
		return nullptr;
	}
	return icon;
}


BBitmap *NewIcon(const entry_ref &ref)
{
	return NewIconBy([&ref](BBitmap *icon, icon_size size) {
		return BNodeInfo::GetTrackerIcon(&ref, icon, size);
	});
}


BBitmap *NewTypeIcon(const char *type)
{
	return NewIconBy([type](BBitmap *icon, icon_size size) {
		const BMimeType mime(type);
		if (mime.GetIcon(icon, size) == B_OK) {
			return B_OK;
		}
		BMimeType super;
		if (mime.GetSupertype(&super) == B_OK && super.GetIcon(icon, size) == B_OK) {
			return B_OK;
		}
		return BMimeType(B_FILE_MIME_TYPE).GetIcon(icon, size);
	});
}


std::string EntryType(const Workspace::DirEntry &entry)
{
	if (entry.directory) {
		return kDirectoryType;
	}
	if (!entry.mimeType.empty()) {
		return entry.mimeType;
	}
	if (entry.link) {
		return kLinkType;
	}
	BMimeType type;
	if (BMimeType::GuessMimeType(entry.name.c_str(), &type) == B_OK && type.Type() != nullptr) {
		return type.Type();
	}
	return B_FILE_MIME_TYPE;
}


} // Studio
