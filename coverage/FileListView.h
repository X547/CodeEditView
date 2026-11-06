#pragma once

#include <string>

#include <ColumnListView.h>

#include "Coverage.h"


namespace Coverage {


// The files of a summary with their coverage, sortable by every column.
// Sends kMsgFileSelected to the target when the selection changes.
class FileListView: public BColumnListView {
public:
	FileListView();

	void SetSummary(const Summary &summary);
	// The path of the selected file; empty if none.
	std::string SelectedPath();
	void Select(const std::string &path);
};


// The longest common directory of the paths, with a trailing slash; empty if
// there is none.
std::string CommonDirectory(const Summary &summary);


} // Coverage
