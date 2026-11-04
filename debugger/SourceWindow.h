#pragma once

#include <Window.h>

#include "Documents.h"


class CodeEditView;


namespace Debugger {


// An editor window for one source document.
class SourceWindow: public BWindow {
private:
	BReference<SourceDocument> fDocument;
	CodeEditView *fEditView;

	void Save();

public:
	explicit SourceWindow(SourceDocument *document);

	const std::string &Path() const {return fDocument->path;}
	// Scrolls to a 1-based line without moving the caret.
	void RevealLine(int32 line);

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Debugger
