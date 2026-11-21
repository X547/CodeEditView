#pragma once

#include <string>

#include <Messenger.h>
#include <Referenceable.h>
#include <Window.h>

#include "Document.h"


class BCheckBox;
class BMenuField;
class BMenuItem;
class BSpinner;


namespace Studio {


// The options of one document, kept with its file: language, indentation,
// MIME type, line endings and BOM. OK sends kMsgFileOptionsChosen to the
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
	BSpinner *fTabWidth {};
	BSpinner *fIndentWidth {};
	BMenuField *fLineEndingField {};
	BCheckBox *fBom {};

	BMenuField *CreateLanguageField(const Workspace::FileAttributes &attributes);
	BMenuField *CreateMimeTypeField(const std::string &mimeType);
	std::string SelectedMimeType() const;
	void UpdateAutomaticLanguage();
	void UpdateIndentWidth();
	void Accept();

public:
	FileOptionsWindow(Workspace::Document *document, const BMessenger &target, BWindow *owner);

	void MessageReceived(BMessage *message) override;
};


} // Studio
