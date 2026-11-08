#pragma once

#include <Window.h>

#include "Common.h"
#include "Document.h"


class CodeEditView;


namespace Debugger {


class DebugSession;


// An editor window for one source document, showing the marks of the
// session.
class SourceWindow: public BWindow {
private:
	BReference<Workspace::Document> fDocument;
	const SessionContext fContext;
	CodeEditView *fEditView;

	void Save();

public:
	SourceWindow(Workspace::Document *document, const SessionContext &context, DebugSession *session);

	const Workspace::Locator &GetLocator() const {return fDocument->GetLocator();}
	// Scrolls to a 1-based line without moving the caret.
	void RevealLine(int32 line);

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Debugger
