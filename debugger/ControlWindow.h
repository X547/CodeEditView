#pragma once

#include <memory>

#include <Window.h>

#include "Protocol.h"


class BButton;
class BCheckBox;
class BFilePanel;
class BStringView;
class BTextControl;


namespace Debugger {


// The main window: starts or attaches to a program and controls it.
class ControlWindow: public BWindow {
private:
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
	ControlWindow(const char *program, const char *arguments);
	~ControlWindow() override;

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Debugger
