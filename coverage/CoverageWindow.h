#pragma once

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include <Referenceable.h>
#include <Window.h>

#include <CodeEditView/TextModel.h>

#include "Coverage.h"
#include "LlvmCov.h"


class BStringView;
class CodeEditView;


namespace Coverage {


class CoverageWorker;
class FileListView;


// The files of a coverage profile and the coverage of the selected file.
class CoverageWindow: public BWindow {
private:
	LlvmCov fCov;
	CoverageWorker *fWorker {};
	FileListView *fFileList {};
	CodeEditView *fEditor {};
	BStringView *fStatus {};
	BReference<CodeEdit::TextModel> fModel;

	std::unique_ptr<Summary> fSummary;
	std::string fPath;
	std::vector<LineState> fLines;
	uint64_t fRequest {};

	void LoadSummary();
	void ShowFile(const std::string &path);
	void SummaryLoaded(BMessage *message);
	void FileLoaded(BMessage *message);
	void GoToUncovered(bool forward);
	void SetStatus(const std::string &text);
	std::string FileStatus() const;

public:
	CoverageWindow(const std::string &binary, const std::string &profile);
	~CoverageWindow() override;

	bool QuitRequested() final;
	void MessageReceived(BMessage *message) final;
	void MenusBeginning() final;
	void WindowActivated(bool active) final;
};


} // Coverage
