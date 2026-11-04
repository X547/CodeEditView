#include "ControlWindow.h"

#include <stdio.h>

#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>

#include "Common.h"


namespace Debugger {


static constexpr uint32 kMsgLaunchClicked = 'CWla';
static constexpr uint32 kMsgAttachClicked = 'CWat';
static constexpr uint32 kMsgAttachTeam = 'CWtm';
static constexpr uint32 kMsgBrowseProgram = 'CWbp';
static constexpr uint32 kMsgProgramChosen = 'CWpc';
static constexpr uint32 kMsgOpenSource = 'CWos';


static BButton *SessionButton(const char *label, uint32 what)
{
	BButton *button = new BButton(label, new BMessage(what));
	button->SetTarget(SessionMessenger());
	return button;
}


ControlWindow::ControlWindow(const char *program, const char *arguments):
	BWindow(BRect(40, 60, 339, 339), "CodeEditDebugger", B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE),
	fProgram(new BTextControl("program", "Program:", program, nullptr)),
	fArguments(new BTextControl("arguments", "Arguments:", arguments, nullptr)),
	fStopAtMain(new BCheckBox("stopAtMain", "Stop at main", nullptr)),
	fLaunchButton(new BButton("Launch", new BMessage(kMsgLaunchClicked))),
	fAttachButton(new BButton("Attach" B_UTF8_ELLIPSIS, new BMessage(kMsgAttachClicked))),
	fContinueButton(SessionButton("Continue", kMsgContinue)),
	fPauseButton(SessionButton("Pause", kMsgPause)),
	fStepOverButton(SessionButton("Step over", kMsgStepOver)),
	fStepIntoButton(SessionButton("Step into", kMsgStepInto)),
	fStepOutButton(SessionButton("Step out", kMsgStepOut)),
	fStopButton(SessionButton("Stop", kMsgStop)),
	fStatus(new BStringView("status", "Not running"))
{
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Open source" B_UTF8_ELLIPSIS, kMsgOpenSource, 'O')
			.AddSeparator()
			.AddItem("Quit", B_QUIT_REQUESTED, 'Q')
		.End();
	menuBar->AddItem(CreateDebugMenu());
	menuBar->AddItem(CreateWindowsMenu());

	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL)
			.SetInsets(B_USE_WINDOW_INSETS)
			.AddGrid()
				.AddTextControl(fProgram, 0, 0)
				.Add(new BButton("Browse" B_UTF8_ELLIPSIS, new BMessage(kMsgBrowseProgram)), 2, 0)
				.AddTextControl(fArguments, 0, 1, B_ALIGN_HORIZONTAL_UNSET, 1, 2)
			.End()
			.AddGroup(B_HORIZONTAL)
				.Add(fStopAtMain)
				.AddGlue()
				.Add(fLaunchButton)
				.Add(fAttachButton)
			.End()
			.AddGrid(B_USE_HALF_ITEM_SPACING, B_USE_HALF_ITEM_SPACING)
				.Add(fContinueButton, 0, 0)
				.Add(fPauseButton, 1, 0)
				.Add(fStopButton, 2, 0)
				.Add(fStepOverButton, 0, 1)
				.Add(fStepIntoButton, 1, 1)
				.Add(fStepOutButton, 2, 1)
			.End()
			.Add(fStatus)
		.End();

	fProgram->MakeFocus(true);
	UpdateControls();

	BMessage watch(kMsgWatch);
	watch.AddMessenger("target", BMessenger(this));
	SessionMessenger().SendMessage(&watch);
}


ControlWindow::~ControlWindow()
{
}


void ControlWindow::UpdateControls()
{
	const bool active = fState != SessionState::Idle;
	const bool stopped = fState == SessionState::Stopped;
	fContinueButton->SetEnabled(stopped);
	fStepOverButton->SetEnabled(stopped);
	fStepIntoButton->SetEnabled(stopped);
	fStepOutButton->SetEnabled(stopped);
	fPauseButton->SetEnabled(fState == SessionState::Running);
	fStopButton->SetEnabled(active);
	fLaunchButton->SetLabel(active ? "Restart" : "Launch");
}


// A menu of the running teams; choosing one attaches to it.
void ControlWindow::ShowTeamMenu()
{
	BPopUpMenu *menu = new BPopUpMenu("teams", false, false);
	int32 cookie = 0;
	team_info info;
	while (get_next_team_info(&cookie, &info) == B_OK) {
		if (info.team == B_SYSTEM_TEAM || info.team == be_app->Team()) {
			continue;
		}
		char label[B_PATH_NAME_LENGTH + 32];
		snprintf(label, sizeof(label), "%" B_PRId32 ": %s", info.team, info.args);
		BMessage *message = new BMessage(kMsgAttachTeam);
		message->AddInt32("team", info.team);
		menu->AddItem(new BMenuItem(label, message));
	}
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	BRect frame = fAttachButton->Bounds();
	fAttachButton->ConvertToScreen(&frame);
	menu->Go(frame.LeftBottom(), true, true, frame, true);
}


void ControlWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kNoticeState:
			fState = static_cast<SessionState>(message->GetInt32("state", 0));
			fStatus->SetText(message->GetString("text", ""));
			UpdateControls();
			break;
		case kNoticeOutput:
		case kNoticeThreads:
		case kNoticeStack:
		case kNoticeScopes:
		case kNoticeVariables:
		case kNoticeBreakpoints:
			break;
		case kMsgLaunchClicked: {
			BMessage launch(kMsgLaunch);
			launch.AddString("program", fProgram->Text());
			launch.AddString("arguments", fArguments->Text());
			launch.AddBool("stopAtMain", fStopAtMain->Value() == B_CONTROL_ON);
			SessionMessenger().SendMessage(&launch);
			break;
		}
		case kMsgAttachClicked:
			ShowTeamMenu();
			break;
		case kMsgAttachTeam: {
			BMessage attach(kMsgAttach);
			attach.AddInt32("team", message->GetInt32("team", -1));
			SessionMessenger().SendMessage(&attach);
			break;
		}
		case kMsgBrowseProgram:
			if (fProgramPanel == nullptr) {
				BMessenger target(this);
				fProgramPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL, &target, nullptr, B_FILE_NODE,
					false, new BMessage(kMsgProgramChosen));
			}
			fProgramPanel->Show();
			break;
		case kMsgProgramChosen: {
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				fProgram->SetText(BPath(&ref).Path());
			}
			break;
		}
		case kMsgOpenSource:
			if (fSourcePanel == nullptr) {
				BMessenger target(be_app_messenger);
				fSourcePanel = std::make_unique<BFilePanel>(B_OPEN_PANEL, &target);
			}
			fSourcePanel->Show();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool ControlWindow::QuitRequested()
{
	if (!gQuitting) {
		be_app->PostMessage(B_QUIT_REQUESTED);
		return false;
	}
	return true;
}


} // Debugger
