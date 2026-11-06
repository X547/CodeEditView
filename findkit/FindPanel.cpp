#include "FindPanel.h"

#include <math.h>

#include <string>

#include <Autolock.h>
#include <Beep.h>
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <Locker.h>
#include <Messenger.h>
#include <ScrollView.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>

#include "FindCommands.h"
#include "FindState.h"
#include "Search.h"


namespace FindKit {


static constexpr uint32 kMsgShowPanel = 'FKsp';
static constexpr uint32 kMsgFocusFind = 'FKff';
static constexpr uint32 kMsgOptionChanged = 'FKoc';

static constexpr int32 kFieldColumns = 50;
static constexpr int32 kFieldRows = 3;

static BLocker sPanelLock("find panel");
static BMessenger sPanel;


static std::string FieldText(CodeEditView *field)
{
	CodeEdit::TextModel &model = *field->Model();
	CodeEdit::ModelLock lock(model);
	return model.Text().ToString();
}


static void SetFieldText(CodeEditView *field, const std::string &text)
{
	if (FieldText(field) == text) {
		return;
	}
	CodeEdit::TextModel &model = *field->Model();
	CodeEdit::ModelLock lock(model);
	model.SetText(text);
}


static float FontLineHeight(const BFont &font)
{
	font_height height;
	font.GetHeight(&height);
	return ceilf(height.ascent + height.descent + height.leading);
}


void FindPanel::ShowPanel()
{
	BAutolock lock(sPanelLock);
	if (!sPanel.IsValid()) {
		FindPanel *panel = new FindPanel();
		sPanel = BMessenger(panel);
		panel->Show();
	}
	sPanel.SendMessage(kMsgShowPanel);
}


FindPanel::FindPanel():
	BWindow(
		BRect(0, 0, 100, 100),
		"Find",
		B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE
	)
{
	fFindField = CreateField("findField");
	fReplaceField = CreateField("replaceField");

	fRegexBox = new BCheckBox("regex", "Regular expression", new BMessage(kMsgOptionChanged));
	fIgnoreCaseBox = new BCheckBox("ignoreCase", "Ignore case", new BMessage(kMsgOptionChanged));
	fWrapBox = new BCheckBox("wrap", "Wrap around", new BMessage(kMsgOptionChanged));
	fBackwardBox = new BCheckBox("backward", "Backwards", new BMessage(kMsgOptionChanged));
	fInSelectionBox = new BCheckBox("inSelection", "Replace all in selection", new BMessage(kMsgOptionChanged));

	// Between the buttons; long messages are truncated, the tool tip has the
	// whole text.
	fStatus = new BStringView("status", "");
	fStatus->SetTruncation(B_TRUNCATE_END);
	fStatus->SetExplicitMinSize(BSize(be_plain_font->StringWidth("Not found"), B_SIZE_UNSET));
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BButton *findButton = new BButton("find", "Find", new BMessage(kMsgFindAgain));

	// A label's maximum width is its text width by default, which would limit
	// the width of its group and so the window's.
	BStringView *findLabel = new BStringView("findLabel", "Find:");
	BStringView *replaceLabel = new BStringView("replaceLabel", "Replace:");
	for (BStringView *label: {findLabel, replaceLabel}) {
		label->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	}

	// The fields share the height the window grows by.
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.Add(findLabel)
			.Add(new BScrollView("findScroll", fFindField, 0, false, true), 1.0f)
		.End()
		.AddGroup(B_VERTICAL, 0, 1.0f)
			.Add(replaceLabel)
			.Add(new BScrollView("replaceScroll", fReplaceField, 0, false, true), 1.0f)
		.End()
		.AddGrid(B_USE_BIG_SPACING, 0)
			.Add(fRegexBox, 0, 0)
			.Add(fIgnoreCaseBox, 1, 0)
			.Add(fWrapBox, 2, 0)
			.Add(fBackwardBox, 0, 1)
			.Add(fInSelectionBox, 1, 1, 2)
			.Add(BSpaceLayoutItem::CreateGlue(), 3, 0)
		.End()
		.AddGroup(B_HORIZONTAL)
			.Add(new BButton("replaceAll", "Replace all", new BMessage(kMsgReplaceAll)))
			.Add(fStatus)
			.Add(new BButton("replace", "Replace", new BMessage(kMsgReplace)))
			.Add(new BButton("replaceAndFind", "Replace & find", new BMessage(kMsgReplaceAndFind)))
			.Add(findButton)
		.End();

	// Enter in a field finds; Shift+Enter reaches the field and inserts a
	// newline.
	SetDefaultButton(findButton);

	AddShortcut('F', B_COMMAND_KEY, new BMessage(kMsgFocusFind));
	AddShortcut('G', B_COMMAND_KEY, new BMessage(kMsgFindAgain));
	AddShortcut('G', B_COMMAND_KEY | B_SHIFT_KEY, new BMessage(kMsgFindAgainBackward));
	AddShortcut('H', B_COMMAND_KEY, new BMessage(kMsgFindSelection));
	AddShortcut('H', B_COMMAND_KEY | B_SHIFT_KEY, new BMessage(kMsgFindSelectionBackward));
	AddShortcut('E', B_COMMAND_KEY, new BMessage(kMsgEnterSearchString));
	AddShortcut('=', B_COMMAND_KEY, new BMessage(kMsgReplace));
	AddShortcut('T', B_COMMAND_KEY, new BMessage(kMsgReplaceAndFind));

	LoadState();
	FindState::SetWatcher(BMessenger(this));
	CenterOnScreen();
}


CodeEditView *FindPanel::CreateField(const char *name)
{
	CodeEditView *field = new CodeEditView(name);
	CodeEdit::EditorSettings settings = field->Settings();
	settings.wordWrap = true;
	field->SetSettings(settings);

	const float width = ceilf(be_fixed_font->StringWidth("M") * kFieldColumns);
	const float height = FontLineHeight(*be_fixed_font) * kFieldRows;
	field->SetExplicitMinSize(BSize(width / 2, height));
	field->SetExplicitPreferredSize(BSize(width, height));
	return field;
}


void FindPanel::LoadState()
{
	const FindSettings settings = FindState::Get();
	SetFieldText(fFindField, settings.findText);
	SetFieldText(fReplaceField, settings.replaceText);
	fRegexBox->SetValue(settings.regex ? B_CONTROL_ON : B_CONTROL_OFF);
	fIgnoreCaseBox->SetValue(settings.ignoreCase ? B_CONTROL_ON : B_CONTROL_OFF);
	fWrapBox->SetValue(settings.wrap ? B_CONTROL_ON : B_CONTROL_OFF);
	fBackwardBox->SetValue(settings.backward ? B_CONTROL_ON : B_CONTROL_OFF);
	fInSelectionBox->SetValue(settings.inSelection ? B_CONTROL_ON : B_CONTROL_OFF);
}


void FindPanel::StoreState()
{
	FindSettings settings;
	settings.findText = NormalizeLineEndings(FieldText(fFindField));
	settings.replaceText = NormalizeLineEndings(FieldText(fReplaceField));
	settings.regex = fRegexBox->Value() == B_CONTROL_ON;
	settings.ignoreCase = fIgnoreCaseBox->Value() == B_CONTROL_ON;
	settings.wrap = fWrapBox->Value() == B_CONTROL_ON;
	settings.backward = fBackwardBox->Value() == B_CONTROL_ON;
	settings.inSelection = fInSelectionBox->Value() == B_CONTROL_ON;
	FindState::Set(settings, false);
}


void FindPanel::Forward(uint32 what)
{
	StoreState();
	const BMessenger host = CodeEdit::EditorCommands::ActiveHost();
	if (!host.IsValid() || host.SendMessage(what) != B_OK) {
		beep();
		SetStatus("No document window", true);
	}
}


void FindPanel::FocusFindField()
{
	fFindField->MakeFocus(true);
	CodeEdit::TextModel &model = *fFindField->Model();
	size_t length = 0;
	{
		CodeEdit::ModelLock lock(model);
		length = model.Text().Length();
	}
	fFindField->SetSelection({0, length});
}


void FindPanel::SetStatus(const char *status, bool error)
{
	fStatus->SetText(status);
	fStatus->SetToolTip(status[0] != '\0' ? status : nullptr);
	fStatus->SetHighUIColor(error ? B_FAILURE_COLOR : B_PANEL_TEXT_COLOR);
}


bool FindPanel::QuitRequested()
{
	// Closed by the user (close button, Escape, Cmd+W): keep the panel. The
	// application quitting asks without a current message.
	const BMessage *message = CurrentMessage();
	if (message != nullptr && message->what == B_QUIT_REQUESTED) {
		StoreState();
		Hide();
		return false;
	}
	FindState::SetWatcher(BMessenger());
	return true;
}


void FindPanel::DispatchMessage(BMessage *message, BHandler *target)
{
	// Tab and Shift+Tab move the focus out of a field (BView's default key
	// handling navigates) instead of inserting a tab; a tab in a pattern is
	// \t in regex mode.
	if (message->what == B_KEY_DOWN && (target == fFindField || target == fReplaceField)) {
		const uint32 modifiers = message->GetInt32("modifiers", 0);
		if (message->GetInt32("raw_char", 0) == B_TAB
			&& (modifiers & (B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY)) == 0) {
			static_cast<BView*>(target)->BView::KeyDown("\t", 1);
			return;
		}
	}
	BWindow::DispatchMessage(message, target);
}


void FindPanel::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgShowPanel:
			if (IsHidden()) {
				Show();
			}
			// Keep the focus when a command from the panel itself (e.g.
			// Replace without a match) asks for the panel.
			if (!IsActive()) {
				FocusFindField();
			}
			Activate();
			break;
		case kMsgFocusFind:
			FocusFindField();
			break;
		case kMsgOptionChanged:
			StoreState();
			break;
		case kMsgFindAgain:
		case kMsgFindAgainBackward:
		case kMsgFindSelection:
		case kMsgFindSelectionBackward:
		case kMsgEnterSearchString:
		case kMsgReplace:
		case kMsgReplaceAndFind:
		case kMsgReplaceAll:
			Forward(message->what);
			break;
		case kMsgFindSettingsChanged:
			LoadState();
			break;
		case kMsgFindStatus:
			SetStatus(message->GetString("status", ""), message->GetBool("error", false));
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void FindPanel::WindowActivated(bool active)
{
	// Commands from the host's menu use the shared state.
	if (!active) {
		StoreState();
	}
	BWindow::WindowActivated(active);
}


} // FindKit
