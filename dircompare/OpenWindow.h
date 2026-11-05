#pragma once

#include <memory>
#include <string>

#include <Window.h>


class BFilePanel;
class BTextControl;


namespace DirCompare {


// Asks for the two directories or files to compare.
class OpenWindow: public BWindow {
private:
	BTextControl *fPaths[2] {};
	std::unique_ptr<BFilePanel> fPanels[2];

	void Browse(int pane);
	void Compare();

public:
	OpenWindow(const std::string &left, const std::string &right);
	~OpenWindow() override;

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // DirCompare
