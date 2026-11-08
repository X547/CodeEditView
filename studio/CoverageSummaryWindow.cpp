#include "CoverageSummaryWindow.h"

#include <Application.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollBar.h>
#include <StringView.h>

#include "Common.h"
#include "CoverageRun.h"
#include "FileListView.h"
#include "Messages.h"
#include "StudioApp.h"


namespace Studio {


static constexpr uint32 kMsgOpenFile = 'CWof';
static constexpr uint32 kMsgReloadCoverage = 'CWrl';


CoverageSummaryWindow::CoverageSummaryWindow(CoverageRun *run):
	BWindow(BRect(120, 120, 719, 519), run->Name().c_str(), B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fRun(run),
	fFileList(new Coverage::FileListView()),
	fStatus(new BStringView("status", ""))
{
	fFileList->SetInvocationMessage(new BMessage(kMsgOpenFile));
	fFileList->SetTarget(this);
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStatus->SetTruncation(B_TRUNCATE_MIDDLE);

	BMenuBar *menuBar = new BMenuBar("menuBar");
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Open", kMsgOpenFile, 'O')
			.AddItem("Reload coverage", kMsgReloadCoverage)
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
			.AddItem("Quit", B_QUIT_REQUESTED, 'Q')
		.End();
	menuBar->FindItem("Quit")->SetTarget(be_app);

	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frame of the list overlaps the menu bar and window borders.
			.SetInsets(-1, -1, -1, 0)
			.Add(fFileList)
		.End()
		.AddGroup(B_HORIZONTAL)
			// The right inset keeps the text clear of the resize corner.
			.SetInsets(B_USE_SMALL_SPACING, 2, B_V_SCROLL_BAR_WIDTH + 2, 2)
			.Add(fStatus)
		.End();

	fRun->AddWatcher(BMessenger(this));
	Update();
}


void CoverageSummaryWindow::Update()
{
	const std::optional<Coverage::Summary> summary = fRun->Summary();
	const std::string selected = fFileList->SelectedPath();
	if (summary.has_value()) {
		fFileList->SetSummary(*summary);
		if (!selected.empty()) {
			fFileList->Select(selected);
		}
	}
	fStatus->SetText(fRun->SummaryStatus().c_str());
}


void CoverageSummaryWindow::OpenSelected()
{
	const std::string path = fFileList->SelectedPath();
	if (path.empty()) {
		return;
	}
	const std::optional<Workspace::Locator> locator = fRun->LocatorOf(path);
	if (!locator.has_value()) {
		return;
	}
	BMessage show(kMsgShowCoverageSource);
	show.AddString("locator", locator->ToString().c_str());
	show.AddInt32("coverage", fRun->Id());
	be_app->PostMessage(&show);
}


void CoverageSummaryWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kNoticeCoverageChanged:
			Update();
			break;
		case kMsgOpenFile:
			OpenSelected();
			break;
		case kMsgReloadCoverage:
			fRun->Reload();
			break;
		case kMsgActivate:
			Activate();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


// Closing the window ends the run, which the application does.
bool CoverageSummaryWindow::QuitRequested()
{
	if (Debugger::gQuitting) {
		return true;
	}
	BMessage close(kMsgCloseCoverage);
	close.AddInt32("coverage", fRun->Id());
	be_app->PostMessage(&close);
	return false;
}


} // Studio
