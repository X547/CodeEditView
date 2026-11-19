#include "LocatorPanel.h"

#include <math.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <utility>
#include <vector>

#include <Autolock.h>
#include <Bitmap.h>
#include <Button.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <ControlLook.h>
#include <GroupLayout.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <StringView.h>

#include "EntryIcons.h"
#include "JobQueue.h"
#include "LocatorFolderMenu.h"
#include "Messages.h"
#include "NameOrder.h"
#include "Provider.h"


namespace Studio {


using Workspace::DirEntry;
using Workspace::Locator;
using Workspace::ProviderError;
using Workspace::Providers;


static constexpr uint32 kMsgListed = 'LPls';
// "locator" (string), from the folder menus.
static constexpr uint32 kMsgNavigate = 'LPnv';
static constexpr uint32 kMsgOpenChosen = 'LPoc';
static constexpr uint32 kMsgUp = 'LPup';
static constexpr uint32 kMsgInvoked = 'LPiv';
static constexpr uint32 kMsgSelected = 'LPsl';


enum {
	kNameColumn,
	kSizeColumn,
	kModifiedColumn,
	kKindColumn,
};


//#pragma mark - Listing

// An entry with what the table shows of it.
struct ListedEntry {
	DirEntry entry;
	// The short description of its type.
	std::string kind;
	std::shared_ptr<BBitmap> icon;
};


struct Listing {
	int32 generation {};
	// Resolved; none if the folder could not be listed.
	std::optional<Locator> folder;
	std::vector<ListedEntry> entries;
	std::string error;
	std::string select;
};


// Shared by a window and its listing jobs, which may outlive it.
struct ListingSlot {
	BLocker lock {"listing"};
	// The listing the window waits for; others are not made.
	std::atomic<int32> wanted {};
	std::unique_ptr<Listing> done;
};


// The application waits for the running listing when it quits; the windows
// do not when they close. Made after the providers, so it is destroyed
// first.
static Workspace::JobQueue &ListingQueue()
{
	Providers::Default();
	static Workspace::JobQueue queue("folder listing");
	return queue;
}


// Lists the folder and finds the types, kinds and icons of its entries,
// which asks the registrar; in a job.
static void ListFolder(
	const std::shared_ptr<ListingSlot> &slot,
	const BMessenger &window,
	const Locator &folder,
	int32 generation,
	const std::string &select
)
{
	if (slot->wanted != generation) {
		return;
	}
	auto listing = std::make_unique<Listing>();
	listing->generation = generation;
	listing->select = select;
	try {
		Workspace::Provider &provider = Providers::Default().Get(folder);
		const Locator resolved = provider.Resolve(folder);
		std::vector<DirEntry> entries = provider.List(resolved);
		listing->folder = resolved;
		// Kinds and icons by type.
		std::map<std::string, std::pair<std::string, std::shared_ptr<BBitmap>>> types;
		for (DirEntry &entry : entries) {
			const std::string type = EntryType(entry);
			auto known = types.find(type);
			if (known == types.end()) {
				char description[B_MIME_TYPE_LENGTH] = "";
				BMimeType(type.c_str()).GetShortDescription(description);
				std::string kind = description[0] != '\0' ? description : type;
				known = types.emplace(
					type,
					std::make_pair(std::move(kind), std::shared_ptr<BBitmap>(NewTypeIcon(type.c_str())))
				).first;
			}
			listing->entries.push_back({std::move(entry), known->second.first, known->second.second});
		}
	} catch (const ProviderError &error) {
		listing->error = error.what();
	}
	{
		BAutolock lock(slot->lock);
		slot->done = std::move(listing);
	}
	window.SendMessage(kMsgListed);
}


//#pragma mark - Columns

namespace {

// Folders come first in every column.
int CompareFolders(bool folder1, bool folder2)
{
	return folder1 == folder2 ? 0 : folder1 ? -1 : 1;
}


class EntryRow: public BRow {
public:
	const std::string name;
	const bool folder;

	EntryRow(const std::string &name, bool folder, float height):
		BRow(height),
		name(name),
		folder(folder)
	{
	}
};


class NameField: public BField {
public:
	const std::string name;
	const bool folder;
	const std::shared_ptr<BBitmap> icon;

	NameField(const std::string &name, bool folder, const std::shared_ptr<BBitmap> &icon):
		name(name),
		folder(folder),
		icon(icon)
	{
	}
};


// The icon and the name, in Tracker's order of names.
class NameColumn: public BTitledColumn {
public:
	NameColumn(const char *title, float width):
		BTitledColumn(title, width, 60, 2000, B_ALIGN_LEFT)
	{
	}

	void DrawField(BField *field, BRect rect, BView *parent) final
	{
		const NameField *name = static_cast<NameField*>(field);
		const BRect icon = IconBounds();
		const float spacing = be_control_look->DefaultLabelSpacing();
		const float left = rect.left + spacing;
		if (name->icon != nullptr) {
			parent->PushState();
			parent->SetDrawingMode(B_OP_ALPHA);
			parent->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
			parent->DrawBitmap(name->icon.get(), BPoint(left, floorf(rect.top + (rect.Height() - icon.Height()) / 2)));
			parent->PopState();
		}
		// DrawString() keeps a margin before the text.
		BRect textRect = rect;
		textRect.left = left + icon.Width() + 1;
		BString text(name->name.c_str());
		parent->TruncateString(&text, B_TRUNCATE_END, textRect.Width() - 2 * spacing);
		DrawString(text.String(), parent, textRect);
	}

	int CompareFields(BField *field1, BField *field2) final
	{
		const NameField *name1 = static_cast<NameField*>(field1);
		const NameField *name2 = static_cast<NameField*>(field2);
		if (const int folders = CompareFolders(name1->folder, name2->folder)) {
			return folders;
		}
		if (DiffKit::NameLess(name1->name, name2->name)) {
			return -1;
		}
		return DiffKit::NameLess(name2->name, name1->name) ? 1 : 0;
	}

	bool AcceptsField(const BField *field) const final
	{
		return dynamic_cast<const NameField*>(field) != nullptr;
	}
};


class SizeField: public BSizeField {
public:
	const bool folder;
	const bool known;

	SizeField(std::optional<uint64_t> size, bool folder):
		BSizeField(static_cast<off_t>(size.value_or(0))),
		folder(folder),
		known(size.has_value())
	{
	}
};


// Sizes as Tracker shows them; nothing for folders and unknown sizes.
class SizeColumn: public BSizeColumn {
public:
	SizeColumn(const char *title, float width):
		BSizeColumn(title, width, 40, 300, B_ALIGN_RIGHT)
	{
	}

	void DrawField(BField *field, BRect rect, BView *parent) final
	{
		if (static_cast<SizeField*>(field)->known) {
			BSizeColumn::DrawField(field, rect, parent);
		}
	}

	int CompareFields(BField *field1, BField *field2) final
	{
		const SizeField *size1 = static_cast<SizeField*>(field1);
		const SizeField *size2 = static_cast<SizeField*>(field2);
		if (const int folders = CompareFolders(size1->folder, size2->folder)) {
			return folders;
		}
		if (size1->known != size2->known) {
			return size1->known ? 1 : -1;
		}
		return BSizeColumn::CompareFields(field1, field2);
	}

	bool AcceptsField(const BField *field) const final
	{
		return dynamic_cast<const SizeField*>(field) != nullptr;
	}
};


class DateField: public BDateField {
public:
	const bool folder;
	const bool known;

	DateField(time_t time, bool known, bool folder):
		BDateField(&time),
		folder(folder),
		known(known)
	{
	}
};


class DateColumn: public BDateColumn {
public:
	DateColumn(const char *title, float width):
		BDateColumn(title, width, 40, 400)
	{
	}

	void DrawField(BField *field, BRect rect, BView *parent) final
	{
		if (static_cast<DateField*>(field)->known) {
			BDateColumn::DrawField(field, rect, parent);
		}
	}

	int CompareFields(BField *field1, BField *field2) final
	{
		const DateField *date1 = static_cast<DateField*>(field1);
		const DateField *date2 = static_cast<DateField*>(field2);
		if (const int folders = CompareFolders(date1->folder, date2->folder)) {
			return folders;
		}
		if (date1->known != date2->known) {
			return date1->known ? 1 : -1;
		}
		return BDateColumn::CompareFields(field1, field2);
	}

	bool AcceptsField(const BField *field) const final
	{
		return dynamic_cast<const DateField*>(field) != nullptr;
	}
};


class KindField: public BStringField {
public:
	const bool folder;

	KindField(const char *kind, bool folder):
		BStringField(kind),
		folder(folder)
	{
	}
};


class KindColumn: public BStringColumn {
public:
	KindColumn(const char *title, float width):
		BStringColumn(title, width, 40, 400, B_TRUNCATE_END)
	{
	}

	int CompareFields(BField *field1, BField *field2) final
	{
		const KindField *kind1 = static_cast<KindField*>(field1);
		const KindField *kind2 = static_cast<KindField*>(field2);
		if (const int folders = CompareFolders(kind1->folder, kind2->folder)) {
			return folders;
		}
		return BStringColumn::CompareFields(field1, field2);
	}

	bool AcceptsField(const BField *field) const final
	{
		return dynamic_cast<const KindField*>(field) != nullptr;
	}
};


// Fits the icons and the text.
float RowHeight()
{
	font_height fontHeight;
	be_plain_font->GetHeight(&fontHeight);
	const float text = ceilf(fontHeight.ascent + fontHeight.descent + fontHeight.leading);
	return std::max(text, IconBounds().Height() + 1) + 4;
}

}


//#pragma mark - EntryListView

class EntryListView: public BColumnListView {
public:
	EntryListView();

	// Columns no entry has a value for are hidden.
	void SetEntries(const std::vector<ListedEntry> &entries, const std::string &select);
	std::vector<const EntryRow*> SelectedRows();
};


EntryListView::EntryListView():
	BColumnListView("entries", B_NAVIGABLE, B_PLAIN_BORDER, true)
{
	SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	AddColumn(new NameColumn("Name", 260), kNameColumn);
	AddColumn(new SizeColumn("Size", 80), kSizeColumn);
	AddColumn(new DateColumn("Modified", 150), kModifiedColumn);
	AddColumn(new KindColumn("Kind", 130), kKindColumn);
	SetSortColumn(ColumnAt(kNameColumn), false, true);
	SetSelectionMessage(new BMessage(kMsgSelected));
	SetInvocationMessage(new BMessage(kMsgInvoked));
}


void EntryListView::SetEntries(const std::vector<ListedEntry> &entries, const std::string &select)
{
	Clear();
	const float height = RowHeight();
	bool timed = false;
	BRow *selected = nullptr;
	for (const ListedEntry &listed : entries) {
		const DirEntry &entry = listed.entry;
		EntryRow *row = new EntryRow(entry.name, entry.directory, height);
		row->SetField(new NameField(entry.name, entry.directory, listed.icon), kNameColumn);
		row->SetField(new SizeField(entry.size, entry.directory), kSizeColumn);
		const time_t modified = static_cast<time_t>(entry.modified.value_or(0));
		row->SetField(new DateField(modified, entry.modified.has_value(), entry.directory), kModifiedColumn);
		row->SetField(new KindField(listed.kind.c_str(), entry.directory), kKindColumn);
		AddRow(row);
		timed = timed || entry.modified.has_value();
		if (entry.name == select) {
			selected = row;
		}
	}
	ColumnAt(kModifiedColumn)->SetVisible(timed);
	if (selected == nullptr) {
		selected = RowAt(0);
	}
	if (selected != nullptr) {
		SetFocusRow(selected, true);
		ScrollTo(selected);
	}
}


std::vector<const EntryRow*> EntryListView::SelectedRows()
{
	std::vector<const EntryRow*> rows;
	for (BRow *row = CurrentSelection(); row != nullptr; row = CurrentSelection(row)) {
		if (const EntryRow *entry = dynamic_cast<const EntryRow*>(row)) {
			rows.push_back(entry);
		}
	}
	return rows;
}


//#pragma mark - LocatorPanel

LocatorPanel::LocatorPanel(const char *title, const BMessenger &target):
	BWindow(BRect(0, 0, 639, 479), title, B_TITLED_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fTarget(target),
	fHeader(new BGroupLayout(B_VERTICAL, B_USE_DEFAULT_SPACING)),
	fFolderMenu(new BPopUpMenu("folder", false, false)),
	fFolderField(new BMenuField("folder", "Folder:", fFolderMenu)),
	fUpButton(new BButton("up", "Up", new BMessage(kMsgUp))),
	fList(new EntryListView()),
	fStatus(new BStringView("status", "")),
	fOpenButton(new BButton("open", "Open", new BMessage(kMsgInvoked))),
	fSlot(std::make_shared<ListingSlot>())
{
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStatus->SetTruncation(B_TRUNCATE_END);
	fFolderField->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fHeader)
		.AddGroup(B_HORIZONTAL)
			.Add(fFolderField, 1)
			.Add(fUpButton)
		.End()
		.Add(fList, 1)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(fOpenButton)
		.End();
	SetDefaultButton(fOpenButton);
	AddShortcut(B_UP_ARROW, B_COMMAND_KEY, new BMessage(kMsgUp));
	AddShortcut(B_DOWN_ARROW, B_COMMAND_KEY, new BMessage(kMsgInvoked));
	CenterOnScreen();
	fList->MakeFocus(true);
	UpdateButtons();
}


void LocatorPanel::SetStatus(const std::string &text)
{
	fStatus->SetText(text.c_str());
}


void LocatorPanel::Navigate(const Locator &folder, const std::string &select)
{
	const int32 generation = ++fGeneration;
	fSlot->wanted = generation;
	SetStatus("Loading" B_UTF8_ELLIPSIS);
	ListingQueue().Post([slot = fSlot, window = BMessenger(this), folder, generation, select] {
		ListFolder(slot, window, folder, generation, select);
	});
}


// A failed listing keeps the folder shown.
void LocatorPanel::ListingDone()
{
	std::unique_ptr<Listing> listing;
	{
		BAutolock lock(fSlot->lock);
		listing = std::move(fSlot->done);
	}
	if (listing == nullptr || listing->generation != fGeneration) {
		return;
	}
	if (!listing->folder.has_value()) {
		SetStatus(listing->error);
		return;
	}
	ShowFolder(*listing->folder);
	fList->SetEntries(listing->entries, listing->select);
	const size_t count = listing->entries.size();
	SetStatus(std::to_string(count) + (count == 1 ? " item" : " items"));
	UpdateButtons();
}


void LocatorPanel::ShowFolder(const Locator &folder)
{
	fFolder = folder;
	fFolderMenu->RemoveItems(0, fFolderMenu->CountItems(), true);
	AddLocatorFolderItems(fFolderMenu, folder, BMessenger(this), {kMsgNavigate, kMsgOpenChosen});
	fFolderMenu->Superitem()->SetLabel(LocatorFolderLabel(folder).c_str());
}


// The folder left is selected.
void LocatorPanel::GoUp()
{
	if (!fFolder.has_value() || fFolder->Parent() == *fFolder) {
		return;
	}
	Navigate(fFolder->Parent(), fFolder->Leaf());
}


// A folder alone opens in the panel; files are sent.
void LocatorPanel::Invoke()
{
	const std::vector<const EntryRow*> rows = fList->SelectedRows();
	if (!fFolder.has_value() || rows.empty()) {
		return;
	}
	if (rows.size() == 1 && rows.front()->folder) {
		Navigate(ChildLocator(*fFolder, rows.front()->name));
		return;
	}
	bool sent = false;
	for (const EntryRow *row : rows) {
		if (!row->folder) {
			BMessage message(kMsgOpenLocator);
			message.AddString("locator", ChildLocator(*fFolder, row->name).ToString().c_str());
			fTarget.SendMessage(&message);
			sent = true;
		}
	}
	if (sent) {
		PostMessage(B_QUIT_REQUESTED);
	}
}


void LocatorPanel::Open(const Locator &file)
{
	BMessage message(kMsgOpenLocator);
	message.AddString("locator", file.ToString().c_str());
	fTarget.SendMessage(&message);
	PostMessage(B_QUIT_REQUESTED);
}


void LocatorPanel::UpdateButtons()
{
	fOpenButton->SetEnabled(fList->CurrentSelection() != nullptr);
	fUpButton->SetEnabled(fFolder.has_value() && fFolder->Parent() != *fFolder);
}


void LocatorPanel::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgListed:
			ListingDone();
			break;
		case kMsgNavigate:
		case kMsgOpenChosen: {
			const std::optional<Locator> locator = Locator::Parse(message->GetString("locator", ""));
			if (!locator.has_value()) {
				break;
			}
			if (message->what == kMsgNavigate) {
				Navigate(*locator);
			} else {
				Open(*locator);
			}
			break;
		}
		case kMsgUp:
			GoUp();
			break;
		case kMsgInvoked:
			Invoke();
			break;
		case kMsgSelected:
			UpdateButtons();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // Studio
