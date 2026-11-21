#include "FileOptionsWindow.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Mime.h>
#include <PopUpMenu.h>
#include <StringView.h>

#include <CodeEditView/Language.h>
#include <CodeEditView/TextModel.h>

#include "Messages.h"


namespace Studio {


static constexpr uint32 kMsgOk = 'FOok';
static constexpr uint32 kMsgMimeTypeChanged = 'FOmt';
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

	// Each starts with the automatic choice: what an .editorconfig file says,
	// else the default.
	const Workspace::EditorConfig config = document->GetEditorConfig();
	const Workspace::Indentation automatic = document->AutomaticIndentation();
	const auto automaticLabel = [](const std::string &value, bool fromConfig) {
		return "Automatic (" + value + (fromConfig ? ", .editorconfig)" : ")");
	};
	const auto chosen = [](const auto &option) {
		return option.has_value() ? std::optional<int32>(static_cast<int32>(*option)) : std::nullopt;
	};

	fIndentField = CreateChoiceField("indent", "Indent with:",
		automaticLabel(automatic.insertSpaces ? "Spaces" : "Tabs", config.insertSpaces.has_value()),
		{{"Tabs", 0}, {"Spaces", 1}}, chosen(options.insertSpaces));

	std::vector<std::pair<std::string, int32>> widths;
	for (int32 width = 1; width <= kMaxWidth; width++) {
		widths.emplace_back(std::to_string(width), width);
	}
	fTabWidth = CreateChoiceField("tabWidth", "Tab width:",
		automaticLabel(std::to_string(automatic.tabWidth), config.tabWidth.has_value()), widths,
		chosen(options.tabWidth));
	widths.insert(widths.begin(), {"Same as tab width", 0});
	fIndentWidth = CreateChoiceField("indentWidth", "Indent width:",
		automaticLabel(automatic.indentSize == 0 ? "same as tab width" : std::to_string(automatic.indentSize),
			config.indentSize.has_value()),
		widths, chosen(options.indentSize));
	fWordWrap = new BCheckBox("wordWrap", "Wrap long lines", nullptr);
	fWordWrap->SetValue(options.wordWrap ? B_CONTROL_ON : B_CONTROL_OFF);

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
			.AddMenuField(fTabWidth, 0, 3)
			.AddMenuField(fIndentWidth, 0, 4)
			.Add(fWordWrap, 1, 5)
			.AddMenuField(fLineEndingField, 0, 6)
			.Add(fBom, 1, 7)
		.End()
		.Add(kept)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED)))
			.Add(okButton)
		.End();
	SetDefaultButton(okButton);
	UpdateAutomaticLanguage();
}


// The automatic choice first, sending no value; the others send "value".
BMenuField *FileOptionsWindow::CreateChoiceField(const char *name, const char *label, const std::string &automatic,
	const std::vector<std::pair<std::string, int32>> &choices, std::optional<int32> chosen)
{
	BPopUpMenu *menu = new BPopUpMenu(name);
	AddMenuItem(menu, automatic, new BMessage(), !chosen.has_value());
	menu->AddSeparatorItem();
	for (const auto &[choiceLabel, value] : choices) {
		BMessage *message = new BMessage();
		message->AddInt32("value", value);
		AddMenuItem(menu, choiceLabel, message, chosen == value);
	}
	return new BMenuField(name, label, menu);
}


// The chosen value; none for the automatic choice.
std::optional<int32> FileOptionsWindow::ChosenValue(BMenuField *field)
{
	int32 value = 0;
	const BMessage *message = MarkedMessage(field);
	if (message == nullptr || message->FindInt32("value", &value) != B_OK) {
		return std::nullopt;
	}
	return value;
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


void FileOptionsWindow::Accept()
{
	BMessage chosen(kMsgFileOptionsChosen);
	chosen.AddString("mimeType", SelectedMimeType().c_str());
	const BMessage *language = MarkedMessage(fLanguageField);
	if (language != nullptr && language->HasString("language")) {
		chosen.AddString("language", language->GetString("language", ""));
	}
	if (const std::optional<int32> insertSpaces = ChosenValue(fIndentField)) {
		chosen.AddBool("insertSpaces", *insertSpaces != 0);
	}
	if (const std::optional<int32> tabWidth = ChosenValue(fTabWidth)) {
		chosen.AddInt32("tabWidth", *tabWidth);
	}
	if (const std::optional<int32> indentSize = ChosenValue(fIndentWidth)) {
		chosen.AddInt32("indentSize", *indentSize);
	}
	chosen.AddBool("wordWrap", fWordWrap->Value() == B_CONTROL_ON);
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
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


} // Studio
