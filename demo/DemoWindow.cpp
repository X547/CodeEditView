#include "DemoWindow.h"

#include <Application.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>

#include <CodeEditView/CodeEditView.h>


DemoWindow::DemoWindow():
	BWindow(
		BRect(100, 100, 739, 579),
		"CodeEditDemo",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_QUIT_ON_WINDOW_CLOSE
	)
{
	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Quit", B_QUIT_REQUESTED, 'Q')
		.End();

	fEditView = new CodeEditView("editView");

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.Add(fEditView);
}


bool DemoWindow::QuitRequested()
{
	return true;
}
