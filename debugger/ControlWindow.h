#pragma once

#include <memory>
#include <string>

#include <Message.h>
#include <Window.h>

#include "Common.h"


class BButton;
class BCheckBox;
class BFilePanel;
class BStringView;
class BTextControl;


namespace Debugger {


// The main window of a session: starts or attaches to a program and
// controls it. Closing it sends the close request to the application.
class ControlWindow: public BWindow {
private:
	const SessionContext fContext;
	BMessage fCloseRequest;
	BTextControl *fProgram;
	BTextControl *fArguments;
	BCheckBox *fStopAtMain;
	BButton *fLaunchButton;
	BButton *fAttachButton;
	BButton *fContinueButton;
	BButton *fPauseButton;
	BButton *fStepOverButton;
	BButton *fStepIntoButton;
	BButton *fStepOutButton;
	BButton *fStopButton;
	BStringView *fStatus;
	std::unique_ptr<BFilePanel> fProgramPanel;
	std::unique_ptr<BFilePanel> fSourcePanel;
	SessionState fState = SessionState::Idle;

	void UpdateControls();
	void ShowTeamMenu();

public:
	ControlWindow(const SessionContext &context, const char *title, const char *program, const char *arguments,
		const BMessage &closeRequest);
	~ControlWindow() override;

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Debugger
