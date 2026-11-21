#include "FileOptionsWindow.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <Spinner.h>
#include <StringView.h>

#include <CodeEditView/Language.h>
#include <CodeEditView/TextModel.h>

#include "Messages.h"


namespace Studio {


static constexpr uint32 kMsgOk = 'FOok';
static constexpr uint32 kMsgMimeTypeChanged = 'FOmt';
static constexpr uint32 kMsgIndentChanged = 'FOin';
static constexpr int32 kMaxWidth = 16;


static BMenuItem *AddMenuItem(BMenu *menu, const std::string &label, BMessage *message, bool marked)
{
	BMenuItem *item = new BMenuItem(label.c_str(), message);
	item->SetMarked(marked);
	menu->AddItem(item);
	return item;
}


static const BMessage *MarkedMessage(BMenuField *field)
{
	BMenuItem *item = field->Menu()->FindMarked();
	return item != nullptr ? item->Message() : nullptr;
}


// "Description (type)", or the type without a description.
static std::string MimeTypeLabel(const char *type)
{
	char description[B_MIME_TYPE_LENGTH];
	if (BMimeType(type).GetShortDescription(description) == B_OK && description[0] != '\0') {
		return std::string(description) + " (" + type + ")";
	}
	return type;
}


FileOptionsWindow::FileOptionsWindow(Workspace::Document *document, const BMessenger &target, BWindow *owner):
	BWindow(
		BRect(0, 0, 399, 299),
		("File options: " + document->GetLocator().DisplayName()).c_str(),
		B_TITLED_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_RESIZABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE
	),
	fDocument(document),
	fTarget(target)
{
	AddToSubset(owner);
	const Workspace::FileAttributes attributes = document->Attributes();
	CodeEdit::LineEnding lineEnding;
	bool hasBom;
	{
		CodeEdit::TextModel &model = *document->Model();
		CodeEdit::ModelLock lock(model);
		lineEnding = model.GetLineEnding();
		hasBom = model.HasBom();
	}
	const Workspace::FileOptions &options = attributes.options;
	const bool readOnly = document->IsReadOnly();

	fLanguageField = CreateLanguageField(attributes);
	fMimeTypeField = CreateMimeTypeField(attributes.mimeType);
	fMimeTypeField->SetEnabled(document->HasAttributes());

	BPopUpMenu *indentMenu = new BPopUpMenu("indent");
	BMessage *tabs = new BMessage(kMsgIndentChanged);
	tabs->AddBool("insertSpaces", false);
	AddMenuItem(indentMenu, "Tabs", tabs, !options.insertSpaces);
	BMessage *spaces = new BMessage(kMsgIndentChanged);
	spaces->AddBool("insertSpaces", true);
	AddMenuItem(indentMenu, "Spaces", spaces, options.insertSpaces);
	fIndentField = new BMenuField("indent", "Indent with:", indentMenu);

	fTabWidth = new BSpinner("tabWidth", nullptr, nullptr);
	fTabWidth->SetRange(1, kMaxWidth);
	fTabWidth->SetValue(static_cast<int32>(options.tabWidth));
	fIndentWidth = new BSpinner("indentWidth", nullptr, nullptr);
	fIndentWidth->SetRange(1, kMaxWidth);
	fIndentWidth->SetValue(static_cast<int32>(options.indentSize > 0 ? options.indentSize : options.tabWidth));

	BPopUpMenu *lineEndingMenu = new BPopUpMenu("lineEnding");
	const std::pair<CodeEdit::LineEnding, const char *> lineEndings[] = {
		{CodeEdit::LineEnding::LF, "LF (Haiku, Unix)"},
		{CodeEdit::LineEnding::CRLF, "CRLF (Windows)"},
		{CodeEdit::LineEnding::CR, "CR (classic Mac OS)"},
	};
	for (const auto &[value, label] : lineEndings) {
		BMessage *message = new BMessage();
		message->AddInt32("lineEnding", static_cast<int32>(value));
		AddMenuItem(lineEndingMenu, label, message, value == lineEnding);
	}
	fLineEndingField = new BMenuField("lineEnding", "Line endings:", lineEndingMenu);
	fLineEndingField->SetEnabled(!readOnly);
	fBom = new BCheckBox("bom", "Write a UTF-8 byte order mark", nullptr);
	fBom->SetValue(hasBom ? B_CONTROL_ON : B_CONTROL_OFF);
	fBom->SetEnabled(!readOnly);

	BStringView *kept = new BStringView("kept", document->HasAttributes()
		? "The options are kept in the file's attributes."
		: "The options are kept until the document is closed.");
	kept->SetHighUIColor(B_PANEL_TEXT_COLOR, B_DARKEN_1_TINT);

	BButton *okButton = new BButton("ok", "OK", new BMessage(kMsgOk));
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid()
			.AddMenuField(fLanguageField, 0, 0)
			.AddMenuField(fMimeTypeField, 0, 1)
			.AddMenuField(fIndentField, 0, 2)
			.Add(new BStringView("tabWidthLabel", "Tab width:"), 0, 3)
			.Add(fTabWidth, 1, 3)
			.Add(new BStringView("indentWidthLabel", "Indent width:"), 0, 4)
			.Add(fIndentWidth, 1, 4)
			.AddMenuField(fLineEndingField, 0, 5)
			.Add(fBom, 1, 6)
		.End()
		.Add(kept)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(okButton)
		.End();
	SetDefaultButton(okButton);
	UpdateAutomaticLanguage();
	UpdateIndentWidth();
}


BMenuField *FileOptionsWindow::CreateLanguageField(const Workspace::FileAttributes &attributes)
{
	const std::optional<std::string> &chosen = attributes.options.language;
	BPopUpMenu *menu = new BPopUpMenu("language");
	fAutomaticItem = AddMenuItem(menu, "Automatic", new BMessage(), !chosen.has_value());
	menu->AddSeparatorItem();
	BMessage *plain = new BMessage();
	plain->AddString("language", "");
	AddMenuItem(menu, "Plain text", plain, chosen.has_value() && chosen->empty());
	for (const std::string &language : CodeEdit::AvailableLanguages()) {
		BMessage *message = new BMessage();
		message->AddString("language", language.c_str());
		AddMenuItem(menu, language, message, chosen.has_value() && *chosen == language);
	}
	// A language no longer available stays chosen.
	if (chosen.has_value() && menu->FindMarked() == nullptr) {
		BMessage *message = new BMessage();
		message->AddString("language", chosen->c_str());
		AddMenuItem(menu, *chosen, message, true);
	}
	return new BMenuField("language", "Language:", menu);
}


// The text types known to the system, and the file's type if it is not one
// of them.
BMenuField *FileOptionsWindow::CreateMimeTypeField(const std::string &mimeType)
{
	std::vector<std::pair<std::string, std::string>> types;
	BMessage installed;
	if (BMimeType::GetInstalledTypes("text", &installed) == B_OK) {
		const char *type = nullptr;
		for (int32 i = 0; installed.FindString("types", i, &type) == B_OK; i++) {
			types.emplace_back(MimeTypeLabel(type), type);
		}
	}
	std::sort(types.begin(), types.end());
	const bool listed = mimeType.empty() || std::any_of(types.begin(), types.end(), [&mimeType](const auto &item) {
		return item.second == mimeType;
	});

	BPopUpMenu *menu = new BPopUpMenu("mimeType");
	BMessage *none = new BMessage(kMsgMimeTypeChanged);
	none->AddString("mimeType", "");
	AddMenuItem(menu, "None", none, mimeType.empty());
	menu->AddSeparatorItem();
	if (!listed) {
		BMessage *message = new BMessage(kMsgMimeTypeChanged);
		message->AddString("mimeType", mimeType.c_str());
		AddMenuItem(menu, MimeTypeLabel(mimeType.c_str()), message, true);
	}
	for (const auto &[label, type] : types) {
		BMessage *message = new BMessage(kMsgMimeTypeChanged);
		message->AddString("mimeType", type.c_str());
		AddMenuItem(menu, label, message, type == mimeType);
	}
	return new BMenuField("mimeType", "File type:", menu);
}


std::string FileOptionsWindow::SelectedMimeType() const
{
	const BMessage *message = MarkedMessage(fMimeTypeField);
	return message != nullptr ? message->GetString("mimeType", "") : "";
}


// The automatic language follows the MIME type chosen.
void FileOptionsWindow::UpdateAutomaticLanguage()
{
	const std::string language = fDocument->LanguageFor(SelectedMimeType());
	fAutomaticItem->SetLabel(("Automatic (" + (language.empty() ? std::string("plain text") : language) + ")").c_str());
	// The field shows the label of the marked item as it was when marked.
	if (fAutomaticItem->IsMarked()) {
		fAutomaticItem->SetMarked(false);
		fAutomaticItem->SetMarked(true);
	}
}


// The indent width applies to indenting with spaces.
void FileOptionsWindow::UpdateIndentWidth()
{
	const BMessage *message = MarkedMessage(fIndentField);
	fIndentWidth->SetEnabled(message != nullptr && message->GetBool("insertSpaces", false));
}


void FileOptionsWindow::Accept()
{
	BMessage chosen(kMsgFileOptionsChosen);
	chosen.AddString("mimeType", SelectedMimeType().c_str());
	const BMessage *language = MarkedMessage(fLanguageField);
	if (language != nullptr && language->HasString("language")) {
		chosen.AddString("language", language->GetString("language", ""));
	}
	const int32 tabWidth = fTabWidth->Value();
	const int32 indentWidth = fIndentWidth->Value();
	chosen.AddInt32("tabWidth", tabWidth);
	chosen.AddInt32("indentSize", indentWidth == tabWidth ? 0 : indentWidth);
	const BMessage *indent = MarkedMessage(fIndentField);
	chosen.AddBool("insertSpaces", indent != nullptr && indent->GetBool("insertSpaces", false));
	const BMessage *lineEnding = MarkedMessage(fLineEndingField);
	chosen.AddInt32("lineEnding", lineEnding != nullptr ? lineEnding->GetInt32("lineEnding", 0) : 0);
	chosen.AddBool("bom", fBom->Value() == B_CONTROL_ON);
	fTarget.SendMessage(&chosen);
	Quit();
}


void FileOptionsWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgOk:
			Accept();
			break;
		case kMsgMimeTypeChanged:
			UpdateAutomaticLanguage();
			break;
		case kMsgIndentChanged:
			UpdateIndentWidth();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // Studio
