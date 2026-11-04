#pragma once

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


static constexpr uint32 kMsgNewDocument = 'Dnew';
static constexpr uint32 kMsgOpenDocument = 'Dopn';
static constexpr uint32 kMsgNewView = 'Dnvw';
static constexpr uint32 kMsgToggleSplit = 'Dspl';
static constexpr uint32 kMsgToggleWordWrap = 'Dwrp';
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

	DemoDocument();
};


class DemoWindow: public BWindow {
private:
	BReference<DemoDocument> fDocument;
	BSplitView *fSplitView {};
	CodeEditScrollView *fSecondPane {};
	std::unique_ptr<BFilePanel> fSavePanel;
	BMenuItem *fWordWrapItem {};
	bool fWordWrap {};

	CodeEditScrollView *CreatePane();
	void UpdateTitle();
	void Save();
	void SaveAs();
	void ToggleSplit();
	void ApplySettings(CodeEditView *editView);

public:
	explicit DemoWindow(DemoDocument *document);
	~DemoWindow() override;

	void MessageReceived(BMessage *message) final;
};


void ShowError(const char *message);
