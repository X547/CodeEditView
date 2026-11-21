#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Messenger.h>
#include <Referenceable.h>
#include <Window.h>

#include "Document.h"


class BCheckBox;
class BMenuField;
class BMenuItem;


namespace Studio {


// The options of one document, kept with its file: language, indentation,
// word wrap, MIME type, line endings and BOM. OK sends kMsgFileOptionsChosen to the
// target (the document's source window), which applies them. Modal to the
// owner window only.
class FileOptionsWindow: public BWindow {
private:
	BReference<Workspace::Document> fDocument;
	BMessenger fTarget;
	BMenuField *fLanguageField {};
	BMenuItem *fAutomaticItem {};
	BMenuField *fMimeTypeField {};
	BMenuField *fIndentField {};
	BMenuField *fTabWidth {};
	BMenuField *fIndentWidth {};
	BCheckBox *fWordWrap {};
	BMenuField *fLineEndingField {};
	BCheckBox *fBom {};

	BMenuField *CreateLanguageField(const Workspace::FileAttributes &attributes);
	BMenuField *CreateMimeTypeField(const std::string &mimeType);
	std::string SelectedMimeType() const;
	void UpdateAutomaticLanguage();
	BMenuField *CreateChoiceField(const char *name, const char *label, const std::string &automatic,
		const std::vector<std::pair<std::string, int32>> &choices, std::optional<int32> chosen);
	static std::optional<int32> ChosenValue(BMenuField *field);
	void Accept();

public:
	FileOptionsWindow(Workspace::Document *document, const BMessenger &target, BWindow *owner);

	void MessageReceived(BMessage *message) override;
};


} // Studio
