#include "Ask.h"
#include "SourceWindow.h"

#include <algorithm>
#include <iterator>
#include <functional>
#include <string>

#include <Alert.h>
#include <Application.h>
#include <Beep.h>
#include <Entry.h>
#include <FilePanel.h>
#include <LayoutBuilder.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <Path.h>
#include <ScrollBar.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringView.h>

#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>

#include "Common.h"
#include "Coverage.h"
#include "CoverageMessages.h"
#include "CoverageRun.h"
#include "FindCommands.h"
#include "Messages.h"
#include "Protocol.h"
#include "OpenGitWindow.h"
#include "OpenLocationWindow.h"
#include "OutlineView.h"
#include "StudioApp.h"


namespace Studio {


using Workspace::Document;
using Workspace::DocumentState;
using Workspace::DocumentStatus;
using Workspace::Locator;
using Workspace::LocatorKind;


static constexpr uint32 kMsgSave = 'SWsv';
static constexpr uint32 kMsgSaveAs = 'SWsa';
static constexpr uint32 kMsgSaveCopy = 'SWsc';
static constexpr uint32 kMsgRevert = 'SWrv';
// From the save panel: "directory" (entry_ref), "name" (string).
static constexpr uint32 kMsgSaveAsChosen = 'SWac';
static constexpr uint32 kMsgSaveCopyChosen = 'SWcc';
static constexpr uint32 kMsgNewView = 'SWnv';
static constexpr uint32 kMsgToggleOutline = 'SWto';
static constexpr uint32 kMsgRefreshOutline = 'SWro';
static constexpr uint32 kMsgFollowSession = 'SWfs';
static constexpr uint32 kMsgFollowCoverage = 'SWfc';
static constexpr uint32 kMsgNextUncovered = 'SWnu';
static constexpr uint32 kMsgPreviousUncovered = 'SWpu';
static constexpr uint32 kMsgNewSession = 'SWns';
static constexpr uint32 kMsgToggleBreakpoint = 'SWtb';
static constexpr uint32 kMsgToggleBreakpointEnabled = 'SWte';

// Commands forwarded to the session followed.
static constexpr uint32 kSessionCommands[] = {
	Debugger::kMsgContinue,
	Debugger::kMsgPause,
	Debugger::kMsgStepOver,
	Debugger::kMsgStepInto,
	Debugger::kMsgStepOut,
	Debugger::kMsgStop,
};


// Asks without blocking; done runs on the window's thread with the button.
static void Ask(BWindow *window, const std::string &text, const char *button0, const char *button1,
	const char *button2, std::function<void(int32 button)> done)
{
	BAlert *alert = new BAlert("CodeEdit", text.c_str(), button0, button1, button2, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	DiffKit::Ask(window, alert, std::move(done));
}


static void ShowError(const std::string &text)
{
	(new BAlert("CodeEdit", text.c_str(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(nullptr);
}


SourceWindow::SourceWindow(Document *document, BRect frame, int32 session, int32 coverage):
	BWindow(frame, "CodeEdit", B_DOCUMENT_WINDOW, B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS),
	fDocument(document),
	fEditView(new CodeEditView("editor", document->Model())),
	fStatus(new BStringView("status", ""))
{
	fEditView->SetTarget(BMessenger(this));
	CodeEdit::MarkStyle frameStyle;
	frameStyle.shape = CodeEdit::MarkShape::Arrow;
	frameStyle.color = {60, 170, 80, 255};
	frameStyle.lineBackground = {60, 170, 80, 64};
	frameStyle.priority = 25;
	fEditView->SetMarkStyle(Debugger::kMarkFrameLine, frameStyle);
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fStatus->SetTruncation(B_TRUNCATE_MIDDLE);

	BMenuBar *menuBar = new BMenuBar("menuBar");
	BMenu *searchMenu = nullptr;
	BLayoutBuilder::Menu<>(menuBar)
		.AddMenu("File")
			// Cmd+N is Debug > Step over.
			.AddItem("New", kMsgNewDocument)
			.AddItem("Open" B_UTF8_ELLIPSIS, kMsgShowOpenFile, 'O')
			.AddItem("Open from Git" B_UTF8_ELLIPSIS, kMsgShowOpenGit)
			.AddItem("Open location" B_UTF8_ELLIPSIS, kMsgShowOpenLocation, 'O', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Save", kMsgSave, 'S')
			.AddItem("Save as" B_UTF8_ELLIPSIS, kMsgSaveAs, 'S', B_SHIFT_KEY)
			.AddItem("Save copy as" B_UTF8_ELLIPSIS, kMsgSaveCopy)
			.AddItem("Revert", kMsgRevert)
			.AddSeparator()
			.AddItem("Close", B_QUIT_REQUESTED, 'W')
			.AddItem("Quit", B_QUIT_REQUESTED, 'Q')
		.End()
		.AddMenu("Edit")
			.AddItem("Undo", B_UNDO, 'Z')
			.AddItem("Redo", B_REDO, 'Z', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Cut", B_CUT, 'X')
			.AddItem("Copy", B_COPY, 'C')
			.AddItem("Paste", B_PASTE, 'V')
			.AddSeparator()
			.AddItem("Select all", B_SELECT_ALL, 'A')
		.End()
		.AddMenu("Search")
			.GetMenu(searchMenu)
		.End()
		.AddMenu("View")
			.AddItem("Outline", kMsgToggleOutline, 'L', B_SHIFT_KEY)
				.GetItem(fOutlineItem)
		.End()
		.AddMenu("Window")
			.AddItem("New view", kMsgNewView, 'N', B_SHIFT_KEY)
			.AddItem("Documents", kMsgShowDocuments, 'D', B_SHIFT_KEY)
			.AddSeparator()
			.AddItem("Open windows in the same stack", kMsgToggleStacking)
		.End();
	FindKit::AddFindItems(searchMenu);
	// Before the Window menu.
	menuBar->AddItem(CreateDebugMenu(), menuBar->CountItems() - 1);
	menuBar->AddItem(CreateCoverageMenu(), menuBar->CountItems() - 1);
	menuBar->FindItem("Quit")->SetTarget(be_app);

	fOutline = new OutlineView();
	fSplitView = new BSplitView(B_HORIZONTAL, 0);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddGroup(B_VERTICAL, 0)
			// The frames of the panes overlap the menu bar and window borders.
			.SetInsets(-1, -1, -1, 0)
			.Add(fSplitView)
		.End()
		.AddGroup(B_HORIZONTAL)
			// The right inset keeps the text clear of the resize corner.
			.SetInsets(B_USE_SMALL_SPACING, 2, B_V_SCROLL_BAR_WIDTH + 2, 2)
			.Add(fStatus)
		.End();

	fSplitView->AddChild(new BScrollView("outlineScroll", fOutline, 0, false, true, B_PLAIN_BORDER), 1);
	fSplitView->AddChild(new CodeEditScrollView("pane", fEditView), 4);
	fOutlineItem->SetMarked(true);

	StudioApp::Get().Registry().AddListener(BMessenger(this));
	Follow(session, coverage);
	UpdateTitle();
	UpdateStatus();
	ScheduleOutlineRefresh();
	fEditView->MakeFocus(true);
}


BMenu *SourceWindow::CreateDebugMenu()
{
	fDebugMenu = new BMenu("Debug");
	fSessionMenu = new BMenu("Session");
	fDebugMenu->AddItem(fSessionMenu);
	fDebugMenu->AddSeparatorItem();
	fDebugMenu->AddItem(new BMenuItem("Continue", new BMessage(Debugger::kMsgContinue), 'R'));
	fDebugMenu->AddItem(new BMenuItem("Pause", new BMessage(Debugger::kMsgPause), 'P'));
	fDebugMenu->AddItem(new BMenuItem("Step over", new BMessage(Debugger::kMsgStepOver), 'N'));
	fDebugMenu->AddItem(new BMenuItem("Step into", new BMessage(Debugger::kMsgStepInto), 'I'));
	fDebugMenu->AddItem(new BMenuItem("Step out", new BMessage(Debugger::kMsgStepOut), 'I', B_SHIFT_KEY));
	fDebugMenu->AddItem(new BMenuItem("Stop", new BMessage(Debugger::kMsgStop)));
	fDebugMenu->AddSeparatorItem();
	fDebugMenu->AddItem(new BMenuItem("Toggle breakpoint", new BMessage(kMsgToggleBreakpoint), 'K'));
	fDebugMenu->AddItem(new BMenuItem("Enable/disable breakpoint", new BMessage(kMsgToggleBreakpointEnabled), 'K',
		B_SHIFT_KEY));
	fDebugMenu->AddSeparatorItem();
	fDebugMenu->AddItem(new BMenuItem("Session control", new BMessage(kMsgShowSessionControl)));
	struct Entry {
		const char *label;
		Debugger::ToolWindowKind window;
		char shortcut;
	};
	static constexpr Entry kEntries[] = {
		{"Console", Debugger::ToolWindowKind::Console, '1'},
		{"Threads", Debugger::ToolWindowKind::Threads, '2'},
		{"Stack", Debugger::ToolWindowKind::Stack, '3'},
		{"Variables", Debugger::ToolWindowKind::Variables, '4'},
		{"Breakpoints", Debugger::ToolWindowKind::Breakpoints, '5'},
	};
	for (const Entry &entry : kEntries) {
		BMessage *message = new BMessage(Debugger::kMsgShowToolWindow);
		message->AddInt32("window", static_cast<int32>(entry.window));
		fDebugMenu->AddItem(new BMenuItem(entry.label, message, entry.shortcut));
	}
	UpdateDebugMenu();
	return fDebugMenu;
}


// The sessions to follow, and the commands enabled with a session.
void SourceWindow::UpdateDebugMenu()
{
	while (BMenuItem *item = fSessionMenu->RemoveItem(static_cast<int32>(0))) {
		delete item;
	}
	BMessage *none = new BMessage(kMsgFollowSession);
	none->AddInt32("session", 0);
	BMenuItem *noneItem = new BMenuItem("None", none);
	noneItem->SetMarked(fSession == 0);
	fSessionMenu->AddItem(noneItem);
	for (const SessionInfo &session : StudioApp::Get().Sessions()) {
		BMessage *follow = new BMessage(kMsgFollowSession);
		follow->AddInt32("session", session.id);
		BMenuItem *item = new BMenuItem(session.name.c_str(), follow);
		item->SetMarked(session.id == fSession);
		fSessionMenu->AddItem(item);
	}
	fSessionMenu->AddSeparatorItem();
	fSessionMenu->AddItem(new BMenuItem("New session", new BMessage(kMsgNewSession)));
	fSessionMenu->SetTargetForItems(this);

	const bool following = fSession != 0;
	for (int32 i = 0; BMenuItem *item = fDebugMenu->ItemAt(i); i++) {
		const uint32 command = item->Command();
		const bool sessionCommand = command == Debugger::kMsgShowToolWindow || command == kMsgShowSessionControl
			|| std::find(std::begin(kSessionCommands), std::end(kSessionCommands), command)
				!= std::end(kSessionCommands);
		if (sessionCommand) {
			item->SetEnabled(following);
		}
	}
}


BMenu *SourceWindow::CreateCoverageMenu()
{
	fCoverageMenu = new BMenu("Coverage");
	fCoverageRunMenu = new BMenu("Run");
	fCoverageMenu->AddItem(fCoverageRunMenu);
	fCoverageMenu->AddItem(new BMenuItem("Coverage files", new BMessage(kMsgShowCoverageSummary)));
	fCoverageMenu->AddSeparatorItem();
	fCoverageMenu->AddItem(new BMenuItem("Next uncovered line", new BMessage(kMsgNextUncovered), 'J'));
	fCoverageMenu->AddItem(new BMenuItem("Previous uncovered line", new BMessage(kMsgPreviousUncovered), 'J',
		B_SHIFT_KEY));
	UpdateCoverageMenu();
	return fCoverageMenu;
}


// The runs to follow, and the commands enabled with a run.
void SourceWindow::UpdateCoverageMenu()
{
	while (BMenuItem *item = fCoverageRunMenu->RemoveItem(static_cast<int32>(0))) {
		delete item;
	}
	BMessage *none = new BMessage(kMsgFollowCoverage);
	none->AddInt32("coverage", 0);
	BMenuItem *noneItem = new BMenuItem("None", none);
	noneItem->SetMarked(fCoverage == 0);
	fCoverageRunMenu->AddItem(noneItem);
	for (const CoverageInfo &coverage : StudioApp::Get().Coverages()) {
		BMessage *follow = new BMessage(kMsgFollowCoverage);
		follow->AddInt32("coverage", coverage.id);
		BMenuItem *item = new BMenuItem(coverage.name.c_str(), follow);
		item->SetMarked(coverage.id == fCoverage);
		fCoverageRunMenu->AddItem(item);
	}
	fCoverageRunMenu->AddSeparatorItem();
	fCoverageRunMenu->AddItem(new BMenuItem("Open coverage" B_UTF8_ELLIPSIS, new BMessage(kMsgShowOpenCoverage)));
	fCoverageRunMenu->SetTargetForItems(this);

	const bool following = fCoverage != 0;
	for (int32 i = 0; BMenuItem *item = fCoverageMenu->ItemAt(i); i++) {
		if (item->Submenu() == nullptr) {
			item->SetEnabled(following);
		}
	}
}


// Shows the marks of the session and the coverage of the run (none: 0)
// beside the shared marks.
void SourceWindow::Follow(int32 session, int32 coverage)
{
	StudioApp &app = StudioApp::Get();
	const Locator &locator = fDocument->GetLocator();
	BReference<CodeEdit::MarkSet> marks;
	if (session != 0) {
		marks = app.SessionMarks(session, fDocument->Model());
	}
	fSession = marks.Get() != nullptr ? session : 0;
	std::vector<BReference<CodeEdit::MarkSet>> markSets;
	if (marks.Get() != nullptr) {
		markSets.push_back(marks);
	}
	fEditView->SetMarkSets(markSets);

	if (fCoverage != 0) {
		app.WatchCoverage(fCoverage, BMessenger(this), false);
	}
	// A run without a path for the document is followed all the same, so
	// its status tells; a run that is gone is not.
	fCoverage = 0;
	for (const CoverageInfo &info : app.Coverages()) {
		if (info.id == coverage) {
			fCoverage = coverage;
		}
	}
	fCoverageSet.Unset();
	if (fCoverage != 0) {
		fCoverageSet = app.CoverageDecorations(fCoverage, locator, fDocument->Model());
		app.WatchCoverage(fCoverage, BMessenger(this), true);
	}
	std::vector<BReference<CodeEdit::DecorationSet>> decorationSets;
	if (fCoverageSet.Get() != nullptr) {
		decorationSets.push_back(fCoverageSet);
	}
	fEditView->SetDecorationSets(decorationSets);

	app.WindowShows(BMessenger(this), locator, fSession, fCoverage);
	// Shortcuts of disabled items do nothing.
	UpdateDebugMenu();
	UpdateCoverageMenu();
	UpdateStatus();
}


// Moves the caret to the first line of the next or previous run of lines
// the coverage shows as not executed.
void SourceWindow::GoToUncovered(bool forward)
{
	if (fCoverageSet.Get() == nullptr) {
		beep();
		return;
	}
	const rgb_color uncovered = Coverage::DefaultColors().uncovered;
	const size_t caretLine = fEditView->CaretLocation().line;
	std::optional<size_t> target;
	{
		CodeEdit::TextModel &model = *fDocument->Model();
		CodeEdit::ModelLock lock(model);
		const CodeEdit::Rope &text = model.Text();
		std::vector<bool> lines(text.LineCount());
		for (const CodeEdit::Decoration &decoration : fCoverageSet->Decorations()) {
			if (decoration.kind != CodeEdit::DecorationKind::LineBackground || decoration.color != uncovered) {
				continue;
			}
			const size_t first = text.LineOfOffset(decoration.start);
			const size_t last = decoration.end > decoration.start ? text.LineOfOffset(decoration.end - 1) : first;
			for (size_t line = first; line <= last && line < lines.size(); line++) {
				lines[line] = true;
			}
		}
		const auto isRunStart = [&lines](size_t line) {
			return lines[line] && (line == 0 || !lines[line - 1]);
		};
		if (forward) {
			for (size_t line = caretLine + 1; line < lines.size() && !target.has_value(); line++) {
				if (isRunStart(line)) {
					target = text.LineStart(line);
				}
			}
		} else {
			for (size_t line = std::min(caretLine, lines.size()); line-- > 0 && !target.has_value();) {
				if (isRunStart(line)) {
					target = text.LineStart(line);
				}
			}
		}
	}
	if (!target.has_value()) {
		beep();
		return;
	}
	fEditView->SetSelection({*target, *target});
	fEditView->ScrollToSelection();
	fEditView->MakeFocus(true);
}


BMessenger SourceWindow::SessionMessenger() const
{
	for (const SessionInfo &session : StudioApp::Get().Sessions()) {
		if (session.id == fSession) {
			return session.messenger;
		}
	}
	return BMessenger();
}


void SourceWindow::Reveal(int32 line)
{
	Activate();
	if (line > 0) {
		fEditView->RevealLine(line - 1);
	}
}


// Edits come in bursts (e.g. undo of a large change); refresh once.
void SourceWindow::ScheduleOutlineRefresh()
{
	if (!fOutlineRefreshPending && fOutlineItem->IsMarked()) {
		fOutlineRefreshPending = true;
		PostMessage(kMsgRefreshOutline);
	}
}


void SourceWindow::RefreshOutline()
{
	fOutlineRefreshPending = false;
	if (!fOutlineItem->IsMarked()) {
		return;
	}
	fOutline->SetOutline(fEditView->Outline());
	fOutline->SelectAt(fEditView->GetSelection().caret);
}


void SourceWindow::JumpToOutlineItem(bool focus)
{
	const OutlineListItem *item = fOutline->SelectedItem();
	if (item == nullptr) {
		return;
	}
	fEditView->SetSelection({item->NameStart(), item->NameEnd()});
	fEditView->ScrollToSelection();
	if (focus) {
		fEditView->MakeFocus(true);
	}
}


void SourceWindow::ToggleOutline()
{
	const bool visible = !fOutlineItem->IsMarked();
	fOutlineItem->SetMarked(visible);
	fSplitView->SetItemCollapsed(0, !visible);
	ScheduleOutlineRefresh();
}


SourceWindow::~SourceWindow()
{
	StudioApp::Get().Registry().RemoveListener(BMessenger(this));
	delete fOpenPanel;
	delete fSavePanel;
}


bool SourceWindow::IsDocumentNotice(BMessage *message) const
{
	return fDocument->GetLocator().ToString() == message->GetString("locator", "");
}


void SourceWindow::UpdateTitle()
{
	const std::string title = (fDocument->IsModified() ? "*" : "") + fDocument->GetLocator().DisplayName();
	if (title != Title()) {
		SetTitle(title.c_str());
	}
}


void SourceWindow::UpdateStatus()
{
	const DocumentStatus status = fDocument->Status();
	const Locator &locator = fDocument->GetLocator();
	std::string text;
	if (status.state == DocumentState::Loading) {
		text = "Loading" B_UTF8_ELLIPSIS;
	} else if (status.state == DocumentState::Failed) {
		text = "Cannot open: " + status.error;
	} else if (status.saving) {
		text = "Saving" B_UTF8_ELLIPSIS;
	} else if (!status.error.empty()) {
		text = "Error: " + status.error;
	} else if (status.changedElsewhere && !status.exists) {
		text = "Deleted by another program; Save writes it again";
	} else if (status.changedElsewhere) {
		text = "Changed by another program; Revert discards your changes";
	} else if (!status.problem.empty()) {
		text = "Read-only: the file " + status.problem;
	} else if (locator.Kind() == LocatorKind::GitCommit) {
		text = "Read-only: " + locator.ToString();
	} else if (locator.Kind() == LocatorKind::Untitled) {
		text = "New document; Save asks where to write it";
	} else {
		text = locator.ToString();
	}
	if (fCoverage != 0) {
		text = StudioApp::Get().CoverageStatus(fCoverage, locator) + " \xe2\x80\x94 " + text;
	}
	if (fSession != 0) {
		text = "Session " + std::to_string(fSession) + " \xe2\x80\x94 " + text;
	}
	fStatus->SetText(text.c_str());
	fStatus->SetToolTip(text.c_str());

	CodeEdit::EditorSettings settings = fEditView->Settings();
	const bool readOnly = fDocument->IsReadOnly() || status.state != DocumentState::Ready;
	if (settings.readOnly != readOnly) {
		settings.readOnly = readOnly;
		fEditView->SetSettings(settings);
	}
}


// An untitled document is saved as a file.
void SourceWindow::Save()
{
	if (fDocument->GetLocator().Kind() == LocatorKind::Untitled) {
		ShowSavePanel(kMsgSaveAsChosen, "Save as", fDocument->GetLocator().Leaf());
		return;
	}
	StudioApp::Get().Registry().Save(fDocument);
}


// The save panel closed (also after a choice, which comes first).
void SourceWindow::SavePanelClosed(BMessage *message)
{
	if (message->GetInt32("old_what", 0) == static_cast<int32>(kMsgSaveAsChosen) && !fSaveAsChosen) {
		// Closing waited for a save that was cancelled.
		fCloseAfterSave = false;
	}
}


void SourceWindow::SetDocument(Document *document)
{
	if (document == fDocument.Get()) {
		return;
	}
	fDocument.SetTo(document);
	fEditView->SetModel(fDocument->Model());
	Follow(fSession, fCoverage);
	UpdateTitle();
	UpdateStatus();
	ScheduleOutlineRefresh();
}


void SourceWindow::SavedNotice(BMessage *message)
{
	const std::string error = message->GetString("error", "");
	const bool closing = fCloseAfterSave;
	fCloseAfterSave = false;
	if (error.empty()) {
		if (closing) {
			PostMessage(B_QUIT_REQUESTED);
		}
		return;
	}
	// Every window of the document gets the notice; the active one asks.
	if (!IsActive() && !closing) {
		return;
	}
	if (message->GetBool("conflict", false)) {
		const std::string question = fDocument->GetLocator().DisplayName()
			+ " was changed by another program since it was read. Overwrite it?";
		Ask(this, question, "Cancel", "Overwrite", nullptr, [this, closing](int32 button) {
			if (button == 1) {
				fCloseAfterSave = closing;
				StudioApp::Get().Registry().Save(fDocument, true);
			}
		});
		return;
	}
	ShowError("Cannot save " + fDocument->GetLocator().DisplayName() + ":\n" + error);
}


std::string SourceWindow::StartDirectory() const
{
	const Locator &locator = fDocument->GetLocator();
	switch (locator.Kind()) {
		case LocatorKind::File:
			return locator.Parent().Path();
		case LocatorKind::GitCommit:
			return locator.Repository();
		default:
			return {};
	}
}


// One panel for all; chosen is the message it sends.
void SourceWindow::ShowSavePanel(uint32 chosen, const char *title, const std::string &name)
{
	if (fSavePanel == nullptr) {
		fSavePanel = new BFilePanel(B_SAVE_PANEL, new BMessenger(this));
	}
	BMessage message(chosen);
	fSavePanel->SetMessage(&message);
	fSaveAsChosen = false;
	if (fSavePanel->Window()->Lock()) {
		fSavePanel->Window()->SetTitle((std::string("CodeEdit: ") + title).c_str());
		fSavePanel->Window()->Unlock();
	}
	const std::string directory = StartDirectory();
	if (!directory.empty()) {
		fSavePanel->SetPanelDirectory(directory.c_str());
	}
	fSavePanel->SetSaveText(name.c_str());
	fSavePanel->Show();
}


void SourceWindow::SaveAs(BMessage *message)
{
	const std::optional<Locator> target = StudioApp::ChosenLocator(*message);
	if (!target.has_value()) {
		return;
	}
	fSaveAsChosen = true;
	BReference<Document> document;
	try {
		document = StudioApp::Get().Registry().SaveAs(fDocument, *target);
	} catch (const Workspace::ProviderError &error) {
		fCloseAfterSave = false;
		ShowError("Cannot save as " + target->Path() + ":\n" + error.what());
		return;
	}
	SetDocument(document);
}


// Another window saved the untitled document shown as a file.
void SourceWindow::DocumentMoved(BMessage *message)
{
	const std::optional<Locator> target = Locator::Parse(message->GetString("to", ""));
	if (target.has_value()) {
		SetDocument(StudioApp::Get().Registry().Open(*target));
	}
}


void SourceWindow::SaveCopy(BMessage *message)
{
	const std::optional<Locator> target = StudioApp::ChosenLocator(*message);
	if (!target.has_value()) {
		return;
	}
	try {
		StudioApp::Get().Registry().SaveCopy(fDocument, *target, BMessenger(this));
	} catch (const Workspace::ProviderError &error) {
		ShowError("Cannot save a copy as " + target->Path() + ":\n" + error.what());
	}
}


void SourceWindow::CopySavedNotice(BMessage *message)
{
	const std::string error = message->GetString("error", "");
	if (error.empty()) {
		return;
	}
	const std::optional<Locator> target = Locator::Parse(message->GetString("locator", ""));
	const std::string name = target.has_value() ? target->DisplayName() : std::string("the copy");
	ShowError("Cannot save " + name + ":\n" + error);
}


void SourceWindow::Revert()
{
	if (fDocument->IsModified()) {
		const std::string question = "Discard your changes to " + fDocument->GetLocator().DisplayName() + "?";
		Ask(this, question, "Cancel", "Discard", nullptr, [this](int32 button) {
			if (button == 1) {
				StudioApp::Get().Registry().Reload(fDocument, true);
			}
		});
		return;
	}
	StudioApp::Get().Registry().Reload(fDocument, true);
}


void SourceWindow::ShowOpenPanel()
{
	if (fOpenPanel == nullptr) {
		fOpenPanel = new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, B_FILE_NODE, true);
		const std::string directory = StartDirectory();
		if (!directory.empty()) {
			fOpenPanel->SetPanelDirectory(directory.c_str());
		}
	}
	fOpenPanel->Show();
}


void SourceWindow::OpenRefs(BMessage *message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BPath path(&ref);
		if (path.InitCheck() == B_OK) {
			StudioApp::Get().ShowDocument(Locator::File(path.Path()), this);
		}
	}
}


void SourceWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case Workspace::kNoticeDocumentChanged:
			if (IsDocumentNotice(message)) {
				UpdateTitle();
				UpdateStatus();
			}
			break;
		case Workspace::kNoticeDocumentSaved:
			if (IsDocumentNotice(message)) {
				SavedNotice(message);
			}
			break;
		case CodeEdit::kMsgContentsChanged:
			UpdateTitle();
			ScheduleOutlineRefresh();
			break;
		case CodeEdit::kMsgCaretMoved:
			fOutline->SelectAt(message->GetUInt64("caret", 0));
			break;
		case kMsgToggleOutline:
			ToggleOutline();
			break;
		case kMsgRefreshOutline:
			RefreshOutline();
			break;
		case kMsgOutlineSelected:
			JumpToOutlineItem(false);
			break;
		case kMsgOutlineInvoked:
			JumpToOutlineItem(true);
			break;
		case Workspace::kNoticeCopySaved:
			CopySavedNotice(message);
			break;
		case Workspace::kNoticeDocumentMoved:
			if (IsDocumentNotice(message)) {
				DocumentMoved(message);
			}
			break;
		case B_CANCEL:
			SavePanelClosed(message);
			break;
		case kMsgNewDocument:
			StudioApp::Get().NewDocument(this);
			break;
		case kMsgSave:
			Save();
			break;
		case kMsgSaveAs:
			ShowSavePanel(kMsgSaveAsChosen, "Save as", fDocument->GetLocator().Leaf());
			break;
		case kMsgSaveAsChosen:
			SaveAs(message);
			break;
		case kMsgSaveCopy:
			ShowSavePanel(kMsgSaveCopyChosen, "Save copy as", fDocument->GetLocator().Leaf());
			break;
		case kMsgSaveCopyChosen:
			SaveCopy(message);
			break;
		case kMsgRevert:
			Revert();
			break;
		case kMsgNewView: {
			ShowOptions options;
			options.newView = true;
			options.session = fSession;
			options.coverage = fCoverage;
			StudioApp::Get().ShowDocument(fDocument->GetLocator(), this, options);
			break;
		}
		case kMsgOpenLocator: {
			const std::optional<Locator> locator = Locator::Parse(message->GetString("locator", ""));
			if (locator.has_value()) {
				ShowOptions options;
				options.newView = message->GetBool("newView", false);
				options.session = message->GetInt32("session", 0);
				options.coverage = message->GetInt32("coverage", 0);
				options.line = message->GetInt32("line", 0);
				StudioApp::Get().ShowDocument(*locator, this, options);
			}
			break;
		}
		case kMsgReveal:
			Reveal(message->GetInt32("line", 0));
			break;
		case kMsgFollowSession:
			Follow(message->GetInt32("session", 0), fCoverage);
			break;
		case kMsgNewSession:
			Follow(StudioApp::Get().NewSession(), fCoverage);
			break;
		case kMsgSessionClosed:
			if (message->GetInt32("session", 0) == fSession) {
				Follow(0, fCoverage);
			}
			break;
		case kMsgFollowCoverage:
			Follow(fSession, message->GetInt32("coverage", 0));
			break;
		case kMsgCoverageClosed:
			if (message->GetInt32("coverage", 0) == fCoverage) {
				Follow(fSession, 0);
			}
			break;
		case kNoticeCoverageChanged:
			UpdateStatus();
			break;
		case kMsgShowCoverageSummary: {
			BMessage forward(kMsgShowCoverageSummary);
			forward.AddInt32("coverage", fCoverage);
			be_app->PostMessage(&forward);
			break;
		}
		case kMsgShowOpenCoverage:
			be_app->PostMessage(kMsgShowOpenCoverage);
			break;
		case kMsgNextUncovered:
		case kMsgPreviousUncovered:
			GoToUncovered(message->what == kMsgNextUncovered);
			break;
		case kMsgShowSessionControl:
		case Debugger::kMsgShowToolWindow: {
			BMessage forward(*message);
			forward.AddInt32("session", fSession);
			be_app->PostMessage(&forward);
			break;
		}
		case Debugger::kMsgContinue:
		case Debugger::kMsgPause:
		case Debugger::kMsgStepOver:
		case Debugger::kMsgStepInto:
		case Debugger::kMsgStepOut:
		case Debugger::kMsgStop:
			SessionMessenger().SendMessage(message);
			break;
		case kMsgToggleBreakpoint:
			Debugger::ToggleBreakpoint(*fDocument->Model(), fEditView->CaretLocation().line);
			break;
		case kMsgToggleBreakpointEnabled:
			Debugger::ToggleBreakpointEnabled(*fDocument->Model(), fEditView->CaretLocation().line);
			break;
		case CodeEdit::kMsgGutterClicked: {
			// Click: breakpoint; Shift + click or secondary button: enable or
			// disable it.
			const size_t line = message->GetUInt64("line", 0);
			const bool alternate = (message->GetInt32("modifiers", 0) & B_SHIFT_KEY) != 0
				|| (message->GetInt32("buttons", 0) & B_SECONDARY_MOUSE_BUTTON) != 0;
			if (alternate) {
				Debugger::ToggleBreakpointEnabled(*fDocument->Model(), line);
			} else {
				Debugger::ToggleBreakpoint(*fDocument->Model(), line);
			}
			break;
		}
		case kMsgShowOpenFile:
			ShowOpenPanel();
			break;
		case B_REFS_RECEIVED:
			OpenRefs(message);
			break;
		case kMsgShowOpenGit:
			(new OpenGitWindow(BMessenger(this), StartDirectory()))->Show();
			break;
		case kMsgShowOpenLocation:
			(new OpenLocationWindow(BMessenger(this)))->Show();
			break;
		case kMsgShowDocuments:
		case kMsgToggleStacking:
			be_app->PostMessage(message);
			break;
		case kMsgActivate:
			Activate();
			break;
		case B_UNDO:
		case B_REDO:
		case B_CUT:
		case B_COPY:
		case B_PASTE:
		case B_SELECT_ALL:
			PostMessage(message, fEditView);
			break;
		default:
			if (!CodeEdit::EditorCommands::Dispatch(message, fEditView)) {
				BWindow::MessageReceived(message);
			}
			break;
	}
}


void SourceWindow::MenusBeginning()
{
	BMenuBar *menuBar = KeyMenuBar();
	CodeEdit::EditorCommands::UpdateMenu(menuBar, fEditView);
	const DocumentStatus status = fDocument->Status();
	const bool ready = status.state == DocumentState::Ready && !status.saving;
	menuBar->FindItem(kMsgSave)->SetEnabled(ready && !fDocument->IsReadOnly());
	menuBar->FindItem(kMsgSaveAs)->SetEnabled(ready);
	menuBar->FindItem(kMsgSaveCopy)->SetEnabled(ready);
	menuBar->FindItem(kMsgRevert)->SetEnabled(!status.saving && status.state != DocumentState::Loading);
	menuBar->FindItem(kMsgToggleStacking)->SetMarked(StudioApp::Get().Stacking());
	UpdateDebugMenu();
	UpdateCoverageMenu();
	BWindow::MenusBeginning();
}


void SourceWindow::WindowActivated(bool active)
{
	if (active) {
		CodeEdit::EditorCommands::SetActiveHost(this);
	}
	BWindow::WindowActivated(active);
}


bool SourceWindow::QuitRequested()
{
	StudioApp &app = StudioApp::Get();
	const Locator &locator = fDocument->GetLocator();
	if (!fCloseAfterSave && fDocument->IsModified() && !fDocument->IsReadOnly()
		&& app.CountWindowsShowing(locator) <= 1) {
		// The window stays open and asks; the answer closes it, or asks the
		// application to quit again if the request came from there.
		if (!fAskingClose) {
			fAskingClose = true;
			// The application asks its windows from its own thread.
			const bool fromApplication = find_thread(nullptr) != Thread();
			const std::string question = "Save changes to " + locator.DisplayName() + "?";
			Ask(this, question, "Cancel", "Don't save", "Save", [this, fromApplication](int32 button) {
				fAskingClose = false;
				if (button == 1) {
					// The registry keeps the document; it takes the file again.
					StudioApp::Get().Registry().Reload(fDocument, true);
					if (fromApplication) {
						be_app->PostMessage(B_QUIT_REQUESTED);
					} else {
						PostMessage(B_QUIT_REQUESTED);
					}
				} else if (button == 2) {
					fCloseAfterSave = true;
					Save();
				}
			});
		}
		return false;
	} else if (fCloseAfterSave) {
		// Closing after a save that has not finished.
		return false;
	}
	app.Registry().RemoveListener(BMessenger(this));
	app.WindowClosing(BMessenger(this));
	return true;
}


} // Studio
