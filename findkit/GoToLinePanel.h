#pragma once

#include <optional>
#include <string>

#include <Messenger.h>
#include <Window.h>

#include <CodeEditView/EditRecord.h>


class BTextControl;
class CodeEditView;


namespace FindKit {


// "line" or "line:column", 1-based, as typed; none if it is neither. The
// result is 0-based.
std::optional<CodeEdit::TextPoint> ParseLocation(const std::string &text);


// Asks for a line (and column) and sends kMsgGoToLine with them to the
// window of the editor it was opened for. Not modal; closes when done.
class GoToLinePanel: public BWindow {
private:
	BMessenger fHost;
	BTextControl *fLocation {};

public:
	// Call on the thread of the editor's window.
	explicit GoToLinePanel(CodeEditView *editor);

	void MessageReceived(BMessage *message) final;
};


} // FindKit
