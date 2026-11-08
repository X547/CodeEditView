#pragma once

#include <Window.h>


class BStringView;

namespace Coverage {
class FileListView;
}


namespace Studio {


class CoverageRun;


// The files of a coverage run with their coverage. Double click opens the
// file in a window following the run; closing the window ends the run.
class CoverageSummaryWindow: public BWindow {
private:
	CoverageRun *fRun;
	Coverage::FileListView *fFileList;
	BStringView *fStatus;

	void Update();
	void OpenSelected();

public:
	explicit CoverageSummaryWindow(CoverageRun *run);

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Studio
