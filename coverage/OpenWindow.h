#pragma once

#include <memory>
#include <string>

#include <Window.h>


class BFilePanel;
class BTextControl;


namespace Coverage {


// Asks for the instrumented binary and its merged profile.
class OpenWindow: public BWindow {
private:
	BTextControl *fPaths[2] {};
	std::unique_ptr<BFilePanel> fPanels[2];

	void Browse(int field);
	void Open();

public:
	OpenWindow(const std::string &binary, const std::string &profile);
	~OpenWindow() override;

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Coverage
