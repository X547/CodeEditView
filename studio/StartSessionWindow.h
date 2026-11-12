#pragma once

#include <memory>
#include <string>

#include <Messenger.h>
#include <Window.h>


class BButton;
class BCheckBox;
class BFilePanel;
class BTextControl;

namespace DiffKit {
class TruncatingLabel;
}


namespace Studio {


// Starts a debug session: the program to launch (with its arguments and
// working directory) or the team to attach to. It makes the session on the
// first try and closes once the session runs, telling the window it was
// opened from to follow it; until then, failures are shown in it and the
// user can correct the settings and try again. Closing it before closes the
// session. The session itself is not tied to it.
class StartSessionWindow: public BWindow {
private:
	BMessenger fFollower;
	int32 fSession {};
	bool fStarting {};
	// Watching reports the state first; Idle is a failure only after this.
	bool fSawStarting {};
	std::string fLastError;
	BTextControl *fProgram {};
	BTextControl *fArguments {};
	BTextControl *fDirectory {};
	BCheckBox *fStopAtMain {};
	BButton *fLaunch {};
	BButton *fAttach {};
	DiffKit::TruncatingLabel *fStatus {};
	std::unique_ptr<BFilePanel> fProgramPanel;
	std::unique_ptr<BFilePanel> fDirectoryPanel;

	// The session to start, made on the first try.
	BMessenger Session();
	void Start(BMessage &command, const std::string &name);
	void SetStarting(bool starting);
	void ShowTeamMenu();
	void StateChanged(BMessage *message);

public:
	// follower: the source window that follows the session once it runs.
	explicit StartSessionWindow(const BMessenger &follower);

	void MessageReceived(BMessage *message) final;
	bool QuitRequested() final;
};


} // Studio
