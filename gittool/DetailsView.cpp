#include "DetailsView.h"

#include <math.h>

#include <algorithm>
#include <utility>
#include <vector>

#include <Clipboard.h>
#include <Directory.h>
#include <File.h>
#include <GridLayout.h>
#include <GridView.h>
#include <MenuItem.h>
#include <Mime.h>
#include <NodeInfo.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>

#include "Messages.h"
#include "Snapshot.h"
#include "TruncatingLabel.h"


namespace GitTool {


static constexpr float kDragThreshold = 3;
// From the context menu of a label: "text" (string) to copy.
static constexpr uint32 kMsgCopyText = 'GTct';
static constexpr const char *kPersonType = "application/x-person";


BMessage CommitDragMessage(const git_oid &id, const BMessenger &source)
{
	BMessage message(kMsgDragCommits);
	message.AddMessenger("source", source);
	message.AddData("commit", B_RAW_TYPE, &id, sizeof(git_oid));
	// Other applications get the commit id as text.
	const std::string text = OidString(id);
	message.AddData("text/plain", B_MIME_TYPE, text.data(), text.size());
	return message;
}


BMessage PersonDragMessage(const std::string &name, const std::string &email)
{
	BMessage message(B_SIMPLE_DATA);
	// Tracker makes a file and asks for its contents (B_COPY_TARGET).
	message.AddInt32("be:actions", B_COPY_TARGET);
	message.AddString("be:types", B_FILE_MIME_TYPE);
	message.AddString("be:filetypes", kPersonType);
	message.AddString("be:type_descriptions", "Person");
	message.AddString("be:clip_name", (name.empty() ? email : name).c_str());
	const std::string text = name.empty() ? email : name + " <" + email + ">";
	message.AddData("text/plain", B_MIME_TYPE, text.data(), text.size());
	return message;
}


void WritePersonFile(const BMessage &reply, const std::string &name, const std::string &email)
{
	entry_ref directoryRef;
	const char *fileName = nullptr;
	if (reply.FindRef("directory", &directoryRef) != B_OK || reply.FindString("name", &fileName) != B_OK) {
		return;
	}
	BDirectory directory(&directoryRef);
	BFile file(&directory, fileName, B_WRITE_ONLY);
	if (file.InitCheck() != B_OK) {
		return;
	}
	BNodeInfo(&file).SetType(kPersonType);
	BString value(name.c_str());
	file.WriteAttrString("META:name", &value);
	value = email.c_str();
	file.WriteAttrString("META:email", &value);
}


//#pragma mark - Labels

static void CopyToClipboard(const std::string &text)
{
	if (!be_clipboard->Lock()) {
		return;
	}
	be_clipboard->Clear();
	be_clipboard->Data()->AddData("text/plain", B_MIME_TYPE, text.data(), text.size());
	be_clipboard->Commit();
	be_clipboard->Unlock();
}


// A label that can be dragged; a click without a drag calls Clicked(), the
// secondary button shows the items of CopyItems().
class DragLabel: public DiffKit::TruncatingLabel {
private:
	bool fPressed {};
	bool fDragged {};
	BPoint fPressPoint;

	void ShowMenu(BPoint where)
	{
		BPopUpMenu *menu = new BPopUpMenu("context", false, false);
		menu->SetAsyncAutoDestruct(true);
		for (const auto &[label, text] : CopyItems()) {
			BMessage *message = new BMessage(kMsgCopyText);
			message->AddString("text", text.c_str());
			menu->AddItem(new BMenuItem(label.c_str(), message));
		}
		menu->SetTargetForItems(this);
		ConvertToScreen(&where);
		menu->Go(where, true, true, true);
	}

protected:
	virtual BMessage DragData() = 0;
	// The items of the context menu: a label and the text it copies.
	virtual std::vector<std::pair<std::string, std::string>> CopyItems() = 0;
	virtual void Clicked(uint32 modifierKeys)
	{
		(void)modifierKeys;
	}

public:
	DragLabel(const char *name, const char *text):
		DiffKit::TruncatingLabel(name, text)
	{
	}

	void MessageReceived(BMessage *message) override
	{
		if (message->what == kMsgCopyText) {
			CopyToClipboard(message->GetString("text", ""));
			return;
		}
		DiffKit::TruncatingLabel::MessageReceived(message);
	}

	void MouseDown(BPoint where) override
	{
		int32 buttons = 0;
		if (Window()->CurrentMessage() != nullptr) {
			Window()->CurrentMessage()->FindInt32("buttons", &buttons);
		}
		if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
			fPressed = false;
			ShowMenu(where);
			return;
		}
		fPressed = true;
		fDragged = false;
		fPressPoint = where;
		SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY);
	}

	void MouseMoved(BPoint where, uint32 transit, const BMessage *dragMessage) override
	{
		DiffKit::TruncatingLabel::MouseMoved(where, transit, dragMessage);
		if (!fPressed || fDragged) {
			return;
		}
		if (fabsf(where.x - fPressPoint.x) <= kDragThreshold && fabsf(where.y - fPressPoint.y) <= kDragThreshold) {
			return;
		}
		fDragged = true;
		BMessage message = DragData();
		BRect rect = Bounds();
		rect.right = std::min(rect.right, rect.left + StringWidth(Text()));
		DragMessage(&message, rect);
	}

	void MouseUp(BPoint where) override
	{
		(void)where;
		if (fPressed && !fDragged) {
			Clicked(modifiers());
		}
		fPressed = false;
	}
};


class CommitLabel: public DragLabel {
private:
	git_oid fCommit;
	BMessenger fTarget;

protected:
	BMessage DragData() final
	{
		return CommitDragMessage(fCommit, BMessenger(Window()));
	}

	std::vector<std::pair<std::string, std::string>> CopyItems() final
	{
		return {{"Copy", OidString(fCommit)}};
	}

	void Clicked(uint32 modifierKeys) final
	{
		if (!IsLink()) {
			return;
		}
		BMessage message(kMsgCommitLinkClicked);
		message.AddData("commit", B_RAW_TYPE, &fCommit, sizeof(git_oid));
		message.AddBool("replace", (modifierKeys & B_CONTROL_KEY) != 0);
		fTarget.SendMessage(&message);
	}

public:
	CommitLabel(const git_oid &commit, const std::string &text, bool link, const BMessenger &target):
		DragLabel("commit", text.c_str()),
		fCommit(commit),
		fTarget(target)
	{
		SetLink(link);
		SetToolTip(link ? "Click: open in a new window. Ctrl+click: show here. Drag: to a history or a commit field. Right-click: copy."
			: "Drag to a history or a commit field. Right-click to copy.");
	}
};


class PersonLabel: public DragLabel {
private:
	std::string fName;
	std::string fEmail;

protected:
	BMessage DragData() final
	{
		return PersonDragMessage(fName, fEmail);
	}

	std::vector<std::pair<std::string, std::string>> CopyItems() final
	{
		return {{"Copy", fName + " <" + fEmail + ">"}, {"Copy e-mail address", fEmail}};
	}

public:
	PersonLabel(const std::string &name, const std::string &email, const std::string &note):
		DragLabel("person", (name + " <" + email + ">" + (note.empty() ? "" : "   " + note)).c_str()),
		fName(name),
		fEmail(email)
	{
		SetToolTip("Drag to Tracker to make a People file, or to a text field. Right-click to copy.");
	}

	void MessageReceived(BMessage *message) final
	{
		if (message->what == B_COPY_TARGET) {
			WritePersonFile(*message, fName, fEmail);
			return;
		}
		DragLabel::MessageReceived(message);
	}
};


//#pragma mark - DetailsView

// A one-line text that can be selected and copied.
static BTextView *ValueText(const std::string &text)
{
	BTextView *view = new BTextView("value");
	view->SetText(text.c_str());
	view->MakeEditable(false);
	view->SetWordWrap(false);
	view->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	view->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
	const float height = ceilf(view->LineHeight(0)) + 1;
	view->SetExplicitMinSize(BSize(50, height));
	view->SetExplicitPreferredSize(BSize(200, height));
	view->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, height));
	return view;
}


DetailsView::DetailsView(const char *name):
	BGroupView(name, B_VERTICAL, B_USE_SMALL_SPACING)
{
	GroupLayout()->SetInsets(B_USE_SMALL_SPACING);
	BGridView *grid = new BGridView(B_USE_SMALL_SPACING, B_USE_HALF_ITEM_SPACING);
	fGrid = grid->GridLayout();
	fGrid->SetColumnWeight(1, 1);
	// Rows keep their heights; the space left is under them.
	grid->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_TOP));
	GroupLayout()->AddView(grid);
	BTextView *message = new BTextView("message");
	message->MakeEditable(false);
	message->SetWordWrap(true);
	fMessage = new BScrollView("messageScroll", message, 0, false, true);
	GroupLayout()->AddView(fMessage, 1);
	fMessage->Hide();

	// Fixed sizes: rows coming and going must not move the views around (as
	// splitters); rows that do not fit are cut off.
	font_height height;
	be_plain_font->GetHeight(&height);
	const float rowHeight = ceilf(height.ascent + height.descent + height.leading) + 4;
	SetExplicitMinSize(BSize(100, 2 * rowHeight));
	SetExplicitPreferredSize(BSize(300, 6 * rowHeight));
	// Without rows that grow, the maximum would be the height of the rows,
	// and splitters would give the rest to the other views.
	SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));
}


void DetailsView::SetTarget(const BMessenger &target)
{
	fTarget = target;
}


void DetailsView::Clear()
{
	BView *grid = fGrid->Owner();
	while (BView *child = grid->ChildAt(0)) {
		child->RemoveSelf();
		delete child;
	}
	fRows = 0;
	SetMessage(std::string());
}


void DetailsView::AddRow(const std::string &label, BView *value)
{
	BStringView *labelView = new BStringView("label", (label + ":").c_str());
	labelView->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER));
	fGrid->AddView(labelView, 0, fRows);
	fGrid->AddView(value, 1, fRows);
	fRows++;
}


void DetailsView::AddText(const std::string &label, const std::string &text)
{
	if (!text.empty()) {
		AddRow(label, ValueText(text));
	}
}


void DetailsView::AddCommit(const std::string &label, const git_oid &id, const std::string &text, bool link)
{
	AddRow(label, new CommitLabel(id, text, link, fTarget));
}


void DetailsView::AddPerson(const std::string &label, const std::string &name, const std::string &email,
	const std::string &note)
{
	if (email.empty()) {
		AddText(label, name + (note.empty() ? "" : "   " + note));
		return;
	}
	AddRow(label, new PersonLabel(name, email, note));
}


void DetailsView::SetMessage(const std::string &text)
{
	BTextView *message = dynamic_cast<BTextView*>(static_cast<BScrollView*>(fMessage)->Target());
	if (message != nullptr) {
		message->SetText(text.c_str());
	}
	if (text.empty() && !fMessage->IsHidden()) {
		fMessage->Hide();
	} else if (!text.empty() && fMessage->IsHidden()) {
		fMessage->Show();
	}
}


} // GitTool
