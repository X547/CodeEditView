#include "StartSessionWindow.h"

#include <stdio.h>

#include <Application.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <FilePanel.h>
#include <GridLayout.h>
#include <GridView.h>
#include <LayoutBuilder.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <TextControl.h>

#include "Messages.h"
#include "Protocol.h"
#include "StudioApp.h"
#include "TruncatingLabel.h"
#include "WindowHelpers.h"


namespace Studio {


static constexpr uint32 kMsgLaunchClicked = 'SSla';
static constexpr uint32 kMsgAttachClicked = 'SSat';
static constexpr uint32 kMsgAttachTeam = 'SStm';
static constexpr uint32 kMsgBrowseProgram = 'SSbp';
static constexpr uint32 kMsgProgramChosen = 'SSpc';
static constexpr uint32 kMsgBrowseDirectory = 'SSbd';
static constexpr uint32 kMsgDirectoryChosen = 'SSdc';


StartSessionWindow::StartSessionWindow(const BMessenger &follower):
	BWindow(
		BRect(0, 0, 499, 199),
		"Start debugging",
		B_TITLED_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_NOT_V_RESIZABLE | B_CLOSE_ON_ESCAPE
	),
	fFollower(follower)
{
	fProgram = new BTextControl("program", "Program:", "", nullptr);
	fArguments = new BTextControl("arguments", "Arguments:", "", nullptr);
	fDirectory = new BTextControl("directory", "Working directory:", "", nullptr);
	fDirectory->SetToolTip("Empty: the program's folder.");
	fStopAtMain = new BCheckBox("stopAtMain", "Stop at main", nullptr);
	fLaunch = new BButton("Launch", new BMessage(kMsgLaunchClicked));
	fAttach = new BButton("Attach" B_UTF8_ELLIPSIS, new BMessage(kMsgAttachClicked));
	fAttach->SetToolTip("Attach to a running team");
	BButton *cancel = new BButton("Cancel", new BMessage(B_QUIT_REQUESTED));
	fStatus = new DiffKit::TruncatingLabel("status", "");
	fStatus->SetExplicitMinSize(BSize(300, B_SIZE_UNSET));

	// A view with a fixed maximum width would fix the window's width.
	fStopAtMain->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BGridView *gridView = new BGridView();
	BGridLayout *grid = gridView->GridLayout();
	// The items of a text control fill the row's height by default; the
	// buttons make the rows taller than the fields.
	const auto addField = [grid](BTextControl *field, int32 row, int32 columns) {
		BLayoutItem *label = field->CreateLabelLayoutItem();
		BLayoutItem *text = field->CreateTextViewLayoutItem();
		label->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));
		text->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));
		grid->AddItem(label, 0, row);
		grid->AddItem(text, 1, row, columns, 1);
	};
	addField(fProgram, 0, 1);
	grid->AddView(new BButton("Browse" B_UTF8_ELLIPSIS, new BMessage(kMsgBrowseProgram)), 2, 0);
	addField(fArguments, 1, 2);
	addField(fDirectory, 2, 1);
	grid->AddView(new BButton("Browse" B_UTF8_ELLIPSIS, new BMessage(kMsgBrowseDirectory)), 2, 2);
	grid->SetColumnWeight(1, 1);

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_INSETS)
		.Add(gridView)
		.Add(fStopAtMain)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(fAttach)
			.AddGlue()
			.Add(cancel)
			.Add(fLaunch)
		.End();
	SetDefaultButton(fLaunch);
	fProgram->MakeFocus(true);
}


BMessenger StartSessionWindow::Session()
{
	if (fSession == 0) {
		fSession = StudioApp::Get().NewSession();
		for (const SessionInfo &session : StudioApp::Get().Sessions()) {
			if (session.id == fSession) {
				BMessage watch(Debugger::kMsgWatch);
				watch.AddMessenger("target", BMessenger(this));
				BMessenger(session.messenger).SendMessage(&watch);
			}
		}
	}
	for (const SessionInfo &session : StudioApp::Get().Sessions()) {
		if (session.id == fSession) {
			return session.messenger;
		}
	}
	return BMessenger();
}


void StartSessionWindow::Start(BMessage &command, const std::string &name)
{
	const BMessenger session = Session();
	if (!session.IsValid()) {
		fStatus->SetHighUIColor(B_FAILURE_COLOR);
		fStatus->SetLabel("The debug session is gone");
		return;
	}
	StudioApp::Get().SetSessionName(fSession, name);
	fLastError.clear();
	fSawStarting = false;
	SetStarting(true);
	session.SendMessage(&command);
}


void StartSessionWindow::SetStarting(bool starting)
{
	fStarting = starting;
	fLaunch->SetEnabled(!starting);
	fAttach->SetEnabled(!starting);
	if (starting) {
		fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR);
		fStatus->SetLabel("Starting" B_UTF8_ELLIPSIS);
	}
}


// A menu of the running teams; choosing one attaches to it.
void StartSessionWindow::ShowTeamMenu()
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
		message->AddString("name", BPath(info.args).Leaf() != nullptr ? BPath(info.args).Leaf() : info.args);
		menu->AddItem(new BMenuItem(label, message));
	}
	menu->SetTargetForItems(this);
	menu->SetAsyncAutoDestruct(true);
	BRect frame = fAttach->Bounds();
	fAttach->ConvertToScreen(&frame);
	menu->Go(frame.LeftBottom(), true, true, frame, true);
}


// The session runs: it is the follower's now. It stopped without running:
// starting failed.
void StartSessionWindow::StateChanged(BMessage *message)
{
	const Debugger::SessionState state = static_cast<Debugger::SessionState>(message->GetInt32("state", 0));
	if (!fStarting) {
		return;
	}
	if (state == Debugger::SessionState::Starting) {
		fSawStarting = true;
		return;
	}
	if (state == Debugger::SessionState::Running || state == Debugger::SessionState::Stopped) {
		BMessage follow(kMsgFollowSession);
		follow.AddInt32("session", fSession);
		fFollower.SendMessage(&follow);
		fSession = 0;
		Quit();
		return;
	}
	if (state == Debugger::SessionState::Idle && fSawStarting) {
		SetStarting(false);
		fStatus->SetHighUIColor(B_FAILURE_COLOR);
		fStatus->SetLabel(fLastError.empty() ? "The program could not be started" : fLastError.c_str());
	}
}


void StartSessionWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case Debugger::kNoticeState:
			StateChanged(message);
			break;
		case Debugger::kNoticeOutput:
			// The reasons a start fails.
			if (fStarting && std::string(message->GetString("category", "")) == "important") {
				fLastError = message->GetString("text", "");
				while (!fLastError.empty() && fLastError.back() == '\n') {
					fLastError.pop_back();
				}
			}
			break;
		case kMsgLaunchClicked: {
			const std::string program = fProgram->Text();
			if (program.empty()) {
				fStatus->SetHighUIColor(B_FAILURE_COLOR);
				fStatus->SetLabel("Choose a program to launch");
				fProgram->MakeFocus(true);
				break;
			}
			BMessage launch(Debugger::kMsgLaunch);
			launch.AddString("program", program.c_str());
			launch.AddString("arguments", fArguments->Text());
			launch.AddString("directory", fDirectory->Text());
			launch.AddBool("stopAtMain", fStopAtMain->Value() == B_CONTROL_ON);
			const char *leaf = BPath(program.c_str()).Leaf();
			Start(launch, leaf != nullptr ? leaf : program);
			break;
		}
		case kMsgAttachClicked:
			ShowTeamMenu();
			break;
		case kMsgAttachTeam: {
			const int32 team = message->GetInt32("team", -1);
			BMessage attach(Debugger::kMsgAttach);
			attach.AddInt32("team", team);
			Start(attach, std::string(message->GetString("name", "")) + " (team " + std::to_string(team) + ")");
			break;
		}
		case kMsgBrowseProgram:
			if (fProgramPanel == nullptr) {
				BMessenger target(this);
				fProgramPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL, &target, nullptr, B_FILE_NODE, false,
					new BMessage(kMsgProgramChosen));
			}
			DiffKit::PlacePanel(fProgramPanel.get(), Frame());
			fProgramPanel->Show();
			break;
		case kMsgBrowseDirectory:
			if (fDirectoryPanel == nullptr) {
				BMessenger target(this);
				fDirectoryPanel = std::make_unique<BFilePanel>(B_OPEN_PANEL, &target, nullptr, B_DIRECTORY_NODE,
					false, new BMessage(kMsgDirectoryChosen));
			}
			DiffKit::PlacePanel(fDirectoryPanel.get(), Frame());
			fDirectoryPanel->Show();
			break;
		case kMsgProgramChosen:
		case kMsgDirectoryChosen: {
			entry_ref ref;
			if (message->FindRef("refs", &ref) == B_OK) {
				(message->what == kMsgProgramChosen ? fProgram : fDirectory)->SetText(BPath(&ref).Path());
			}
			break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


// A session made for a start that did not run is closed with the window.
bool StartSessionWindow::QuitRequested()
{
	if (fSession != 0) {
		BMessage close(kMsgCloseSession);
		close.AddInt32("session", fSession);
		be_app->PostMessage(&close);
		fSession = 0;
	}
	return true;
}


} // Studio
