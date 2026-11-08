#pragma once

#include <vector>

#include <Window.h>

#include "Locator.h"


class BListView;


namespace Studio {


// The open documents of the application, and the commands to open more.
// Shown at start without documents to open.
class DocumentsWindow: public BWindow {
private:
	BListView *fList;
	std::vector<Workspace::Locator> fLocators;

	void Refresh();

public:
	DocumentsWindow();
	~DocumentsWindow() override;

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Studio
