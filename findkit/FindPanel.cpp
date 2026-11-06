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
#include <RadioButton.h>
#include <ScrollView.h>
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

	fExactButton = new BRadioButton("exact", "Exact", new BMessage(kMsgOptionChanged));
	fRegexButton = new BRadioButton("regex", "Regular expression", new BMessage(kMsgOptionChanged));
	fIgnoreCaseBox = new BCheckBox("ignoreCase", "Ignore case", new BMessage(kMsgOptionChanged));
	fWrapBox = new BCheckBox("wrap", "Wrap around", new BMessage(kMsgOptionChanged));
	fBackwardBox = new BCheckBox("backward", "Backwards", new BMessage(kMsgOptionChanged));
	fInSelectionBox = new BCheckBox("inSelection", "Replace all in selection", new BMessage(kMsgOptionChanged));

	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMinSize(BSize(B_SIZE_UNSET, FontLineHeight(*be_plain_font)));
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BButton *findButton = new BButton("find", "Find", new BMessage(kMsgFindAgain));
	BStringView *findLabel = new BStringView("findLabel", "Find:");
	BStringView *replaceLabel = new BStringView("replaceLabel", "Replace:");
	findLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
	replaceLabel->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
			.Add(findLabel, 0, 0)
			.Add(new BScrollView("findScroll", fFindField, 0, false, true), 1, 0)
			.Add(replaceLabel, 0, 1)
			.Add(new BScrollView("replaceScroll", fReplaceField, 0, false, true), 1, 1)
		.End()
		.AddGroup(B_HORIZONTAL, B_USE_BIG_SPACING)
			.AddGroup(B_VERTICAL, 0)
				.Add(fExactButton)
				.Add(fRegexButton)
				.AddGlue()
			.End()
			.AddGroup(B_VERTICAL, 0)
				.Add(fIgnoreCaseBox)
				.Add(fWrapBox)
				.Add(fBackwardBox)
				.Add(fInSelectionBox)
			.End()
			.AddGlue()
		.End()
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL)
			.Add(new BButton("replaceAll", "Replace all", new BMessage(kMsgReplaceAll)))
			.AddGlue()
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
	fExactButton->SetValue(settings.regex ? B_CONTROL_OFF : B_CONTROL_ON);
	fRegexButton->SetValue(settings.regex ? B_CONTROL_ON : B_CONTROL_OFF);
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
	settings.regex = fRegexButton->Value() == B_CONTROL_ON;
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
