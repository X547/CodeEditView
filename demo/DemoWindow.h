#pragma once

#include <initializer_list>
#include <memory>

#include <Path.h>
#include <Referenceable.h>
#include <Window.h>

#include <CodeEditView/TextModel.h>


class BFilePanel;
class BMenuItem;
class BSplitView;
class CodeEditScrollView;
class CodeEditView;
class OutlineView;


static constexpr uint32 kMsgNewDocument = 'Dnew';
static constexpr uint32 kMsgOpenDocument = 'Dopn';
static constexpr uint32 kMsgNewView = 'Dnvw';
static constexpr uint32 kMsgToggleSplit = 'Dspl';
static constexpr uint32 kMsgToggleWordWrap = 'Dwrp';
static constexpr uint32 kMsgToggleOutline = 'Dotg';
static constexpr uint32 kMsgRefreshOutline = 'Dorf';
static constexpr uint32 kMsgToggleBookmark = 'Dtbm';
static constexpr uint32 kMsgNextBookmark = 'Dnbm';
static constexpr uint32 kMsgPreviousBookmark = 'Dpbm';
static constexpr uint32 kMsgToggleBreakpoint = 'Dtbp';
static constexpr uint32 kMsgEnableBreakpoint = 'Debp';
static constexpr uint32 kMsgStep = 'Dstp';
static constexpr uint32 kMsgContinue = 'Dcnt';
static constexpr uint32 kMsgStop = 'Dsto';
static constexpr uint32 kMsgSave = 'Dsav';
static constexpr uint32 kMsgSaveAs = 'Dsas';
static constexpr uint32 kMsgWindowOpened = 'Dwop';
static constexpr uint32 kMsgWindowClosed = 'Dwcl';
static constexpr uint32 kMsgQuitApplication = 'Dqui';


// A text model with its file, shared by all windows showing it.
class DemoDocument: public BReferenceable {
public:
	BReference<CodeEdit::TextModel> model;
	BPath path;
	// Mark of the fake debugger's execution line, if running.
	CodeEdit::MarkId execMark {};

	DemoDocument();
};


class DemoWindow: public BWindow {
private:
	BReference<DemoDocument> fDocument;
	BSplitView *fMainSplitView {};
	BSplitView *fSplitView {};
	CodeEditScrollView *fFirstPane {};
	CodeEditScrollView *fSecondPane {};
	OutlineView *fOutline {};
	BMenuItem *fOutlineItem {};
	// The edit view the outline follows: the last one with a caret move.
	CodeEditView *fActiveView {};
	bool fOutlineRefreshPending {};
	std::unique_ptr<BFilePanel> fSavePanel;
	BMenuItem *fWordWrapItem {};
	bool fWordWrap {};

	CodeEditView *CommandTarget();
	CodeEditScrollView *CreatePane();
	void UpdateTitle();
	void Save();
	void SaveAs();
	void ToggleSplit();
	void ApplySettings(CodeEditView *editView);
	void ScheduleOutlineRefresh();
	void RefreshOutline();
	void JumpToOutlineItem(bool focus);
	size_t CaretLine();
	void ToggleMark(size_t line, std::initializer_list<uint32_t> kinds);
	void ToggleBreakpointEnabled(size_t line);
	void GoToBookmark(bool forward);
	void MoveExecLine(bool toBreakpoint);
	void StopExecution();

public:
	explicit DemoWindow(DemoDocument *document);
	~DemoWindow() override;

	void MessageReceived(BMessage *message) final;
	void MenusBeginning() final;
	void WindowActivated(bool active) final;
};


void ShowError(const char *message);
