#pragma once

#include <string>

#include <Rect.h>

#include "Provider.h"


class BBitmap;
struct entry_ref;


namespace Studio {


// The type of folders (Tracker's B_DIRECTORY_MIME_TYPE, not public).
constexpr const char *kDirectoryType = "application/x-vnd.Be-directory";
// The type of symbolic links (Tracker's B_LINK_MIME_TYPE, not public).
constexpr const char *kLinkType = "application/x-vnd.Be-symlink";


// The bounds of an icon in menus and lists: B_MINI_ICON scaled with the font.
BRect IconBounds();

// The Tracker icon of a file or folder on disk; nullptr if there is none.
BBitmap *NewIcon(const entry_ref &ref);
// The icon of a MIME type, else of its supertype, else of a generic file;
// nullptr if there is none.
BBitmap *NewTypeIcon(const char *type);

// The MIME type an entry of a listing is shown with: folders and links as
// Tracker shows them, a file by its own type, else by one guessed from its
// name. Guessing asks the registrar, so many entries are typed off the
// window threads.
std::string EntryType(const Workspace::DirEntry &entry);


} // Studio
