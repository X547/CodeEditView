#include "CoverageWindow.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <algorithm>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

#include <Application.h>
#include <Beep.h>
#include <File.h>
#include <LayoutBuilder.h>
#include <Looper.h>
#include <MenuBar.h>
#include <Path.h>
#include <ScrollBar.h>
#include <SplitView.h>
#include <StringView.h>

#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>
#include <CodeEditView/Language.h>

#include "CoverageMessages.h"
#include "FileListView.h"
#include "FindCommands.h"


namespace Coverage {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


// Runs llvm-cov off the window thread. Results are heap objects passed by
// pointer; the window owns them once it receives the reply.
class CoverageWorker: public BLooper {
private:
	LlvmCov fCov;
	BMessenger fTarget;

	template<typename Result, typename Load>
	void Reply(BMessage &reply, Load load)
	{
		try {
			std::unique_ptr<Result> result = std::make_unique<Result>(load());
			reply.AddPointer("result", result.get());
			if (fTarget.SendMessage(&reply) == B_OK) {
				result.release();
			}
		} catch (const std::exception &error) {
			reply.AddString("error", error.what());
			fTarget.SendMessage(&reply);
		}
	}

public:
	CoverageWorker(const LlvmCov &cov, const BMessenger &target):
		BLooper("coverage worker"),
		fCov(cov),
		fTarget(target)
	{
	}

	void MessageReceived(BMessage *message) final
	{
		switch (message->what) {
			case kMsgLoadSummary: {
				BMessage reply(kMsgSummaryLoaded);
				Reply<Summary>(reply, [this]() {return fCov.LoadSummary();});
				break;
			}
			case kMsgLoadFile: {
				const std::string path = message->GetString("path", "");
				BMessage reply(kMsgFileLoaded);
				reply.AddString("path", path.c_str());
				reply.AddUInt64("generation", message->GetUInt64("generation", 0));
				Reply<FileCoverage>(reply, [this, &path]() {return fCov.LoadFile(path);});
				break;
			}
			default:
				BLooper::MessageReceived(message);
				break;
		}
	}
};


static bool IsNewer(const std::string &path, const std::string &than)
{
	struct stat pathStat;
	struct stat thanStat;
	return stat(path.c_str(), &pathStat) == 0 && stat(than.c_str(), &thanStat) == 0
		&& pathStat.st_mtime > thanStat.st_mtime;
}


CoverageWindow::CoverageWindow(const std::string &binary, const std::string &profile):
	BWindow(
		BRect(100, 100, 1099, 749),
		"Coverage",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fCov(binary, profile),
	fModel(new TextModel(), true)
{
	BPath binaryPath(binary.c_str());
	SetTitle((std::string("Coverage: ") + (binaryPath.Leaf() != nullptr ? binaryPath.Leaf() : binary.c_str())).c_str());

	BMenuBar *menuBar = new BMenuBar("menuBar");
	BMenu *searchMenu = nullptr;
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			.AddItem("Open" B_UTF8_ELLIPSIS, kMsgShowOpenWindow, 'O')
			.AddItem("Reload", kMsgReload, 'R')
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
			.AddItem("Quit", B_QUIT_REQUESTED, 'Q')
		.End()
		.AddMenu("Edit")
			.AddItem("Copy", B_COPY, 'C')
			.AddItem("Select all", B_SELECT_ALL, 'A')
		.End()
		.AddMenu("Search")
			.GetMenu(searchMenu)
		.End()
		.AddMenu("Go")
			.AddItem("Next uncovered line", kMsgNextUncovered, 'J')
			.AddItem("Previous uncovered line", kMsgPreviousUncovered, 'J', B_SHIFT_KEY)
		.End();
	FindKit::AddFindItems(searchMenu);
	// Quit goes to the application, Close to this window.
	menuBar->FindItem("Quit")->SetTarget(be_app);
	menuBar->FindItem("Open" B_UTF8_ELLIPSIS)->SetTarget(be_app);

	fFileList = new FileListView();
	fFileList->SetTarget(this);
	fEditor = new CodeEditView("editor", fModel.Get());
	CodeEdit::EditorSettings settings = fEditor->Settings();
	settings.readOnly = true;
	fEditor->SetSettings(settings);
	fStatus = new BStringView("status", "Loading coverage" B_UTF8_ELLIPSIS);
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStatus->SetTruncation(B_TRUNCATE_MIDDLE);

	BSplitView *split = new BSplitView(B_HORIZONTAL, 0);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frames of the panes overlap the menu bar and window borders.
			.SetInsets(-1, -1, -1, 0)
			.Add(split)
		.End()
		.AddGroup(B_HORIZONTAL)
			// The right inset keeps the text clear of the window's resize
			// corner.
			.SetInsets(B_USE_SMALL_SPACING, 2, B_V_SCROLL_BAR_WIDTH + 2, 2)
			.Add(fStatus)
		.End();
	split->AddChild(fFileList, 2);
	split->AddChild(new CodeEditScrollView("pane", fEditor), 3);

	fWorker = new CoverageWorker(fCov, BMessenger(this));
	fWorker->Run();
	LoadSummary();
}


CoverageWindow::~CoverageWindow()
{
}


void CoverageWindow::LoadSummary()
{
	SetStatus("Loading coverage" B_UTF8_ELLIPSIS);
	fWorker->PostMessage(kMsgLoadSummary);
}


void CoverageWindow::ShowFile(const std::string &path)
{
	if (path == fPath) {
		return;
	}
	fPath = path;
	fLines.clear();
	fGeneration++;
	BReference<TextModel> model(new TextModel(), true);
	try {
		BFile file(path.c_str(), B_READ_ONLY);
		const status_t status = file.InitCheck();
		if (status != B_OK) {
			throw std::runtime_error(std::string("Cannot read ") + path + ": " + strerror(status));
		}
		ModelLock lock(*model);
		model->Load(file);
		model->SetLanguage(CodeEdit::LanguageForPath(path));
	} catch (const std::exception &error) {
		fModel = model;
		fEditor->SetModel(fModel.Get());
		SetStatus(error.what());
		return;
	}
	fModel = model;
	fEditor->SetModel(fModel.Get());

	RequestFileCoverage();
	SetStatus(FileStatus() + " · loading" B_UTF8_ELLIPSIS);
}


// At most one llvm-cov run for a file at a time: while one runs, selecting
// other files only changes what is requested when it ends.
void CoverageWindow::RequestFileCoverage()
{
	if (fFileRequestPending || fPath.empty()) {
		return;
	}
	fFileRequestPending = true;
	BMessage request(kMsgLoadFile);
	request.AddString("path", fPath.c_str());
	request.AddUInt64("generation", fGeneration);
	fWorker->PostMessage(&request);
}


void CoverageWindow::SummaryLoaded(BMessage *message)
{
	void *pointer = nullptr;
	if (message->FindPointer("result", &pointer) != B_OK) {
		SetStatus(std::string("llvm-cov: ") + message->GetString("error", "failed"));
		return;
	}
	fSummary.reset(static_cast<Summary*>(pointer));
	fFileList->SetSummary(*fSummary);
	if (fPath.empty()) {
		SetStatus(
			"Total: " + FormatSummary(fSummary->totals) + " · " + std::to_string(fSummary->files.size()) + " files"
		);
	}
}


void CoverageWindow::FileLoaded(BMessage *message)
{
	void *pointer = nullptr;
	message->FindPointer("result", &pointer);
	std::unique_ptr<FileCoverage> coverage(static_cast<FileCoverage*>(pointer));
	fFileRequestPending = false;
	if (message->GetUInt64("generation", 0) != fGeneration) {
		// Another file was selected (or reloaded) meanwhile.
		RequestFileCoverage();
		return;
	}
	if (coverage == nullptr) {
		SetStatus(std::string("llvm-cov: ") + message->GetString("error", "failed"));
		return;
	}

	CodeEdit::Rope text;
	{
		ModelLock lock(*fModel);
		text = fModel->Text();
	}
	fLines = ComputeLineStates(coverage->segments, text.LineCount());
	fEditor->SetDecorations(MakeDecorations(*coverage, fLines, text, DefaultColors()));
	SetStatus(FileStatus());
}


void CoverageWindow::GoToUncovered(bool forward)
{
	// The first lines of runs of uncovered lines.
	const auto isRunStart = [this](size_t line) {
		return fLines[line] == LineState::Uncovered && (line == 0 || fLines[line - 1] != LineState::Uncovered);
	};
	const size_t caretLine = fEditor->CaretLocation().line;
	std::optional<size_t> target;
	if (forward) {
		for (size_t line = caretLine + 1; line < fLines.size() && !target.has_value(); line++) {
			if (isRunStart(line)) {
				target = line;
			}
		}
	} else {
		for (size_t line = std::min(caretLine, fLines.size()); line-- > 0 && !target.has_value();) {
			if (isRunStart(line)) {
				target = line;
			}
		}
	}
	if (!target.has_value()) {
		beep();
		return;
	}
	size_t offset = 0;
	{
		ModelLock lock(*fModel);
		offset = fModel->Text().LineStart(*target);
	}
	fEditor->SetSelection({offset, offset});
	fEditor->ScrollToSelection();
	fEditor->MakeFocus(true);
}


void CoverageWindow::SetStatus(const std::string &text)
{
	fStatus->SetText(text.c_str());
	fStatus->SetToolTip(text.empty() ? nullptr : text.c_str());
}


std::string CoverageWindow::FileStatus() const
{
	std::string status = fPath;
	if (fSummary != nullptr) {
		status = fPath.substr(CommonDirectory(*fSummary).size());
		for (const FileSummary &file: fSummary->files) {
			if (file.path == fPath) {
				status += ": " + FormatSummary(file);
				break;
			}
		}
	}
	if (IsNewer(fPath, fCov.Profile())) {
		status += " · the source is newer than the profile";
	}
	return status;
}


bool CoverageWindow::QuitRequested()
{
	if (fWorker->Lock()) {
		fWorker->Quit();
	}
	be_app->PostMessage(kMsgWindowClosed);
	return true;
}


void CoverageWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgSummaryLoaded:
			SummaryLoaded(message);
			break;
		case kMsgFileLoaded:
			FileLoaded(message);
			break;
		case kMsgFileSelected: {
			const std::string path = fFileList->SelectedPath();
			if (!path.empty()) {
				ShowFile(path);
			}
			break;
		}
		case kMsgReload: {
			const std::string path = fPath;
			fPath.clear();
			LoadSummary();
			if (!path.empty()) {
				ShowFile(path);
			}
			break;
		}
		case kMsgNextUncovered:
		case kMsgPreviousUncovered:
			GoToUncovered(message->what == kMsgNextUncovered);
			break;
		case B_COPY:
		case B_SELECT_ALL:
			PostMessage(message, fEditor);
			break;
		default:
			if (!CodeEdit::EditorCommands::Dispatch(message, fEditor)) {
				BWindow::MessageReceived(message);
			}
			break;
	}
}


void CoverageWindow::MenusBeginning()
{
	CodeEdit::EditorCommands::UpdateMenu(KeyMenuBar(), fEditor);
	BWindow::MenusBeginning();
}


void CoverageWindow::WindowActivated(bool active)
{
	if (active) {
		CodeEdit::EditorCommands::SetActiveHost(this);
	}
	BWindow::WindowActivated(active);
}


} // Coverage
