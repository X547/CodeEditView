#include "FindCommands.h"

#include <algorithm>
#include <optional>
#include <string>

#include <Beep.h>
#include <InterfaceDefs.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>

#include "FindPanel.h"
#include "GoToLinePanel.h"
#include "FindState.h"
#include "ModelSearch.h"
#include "Search.h"


namespace FindKit {


using CodeEdit::EditorCommandSet;
using CodeEdit::ModelLock;


static void ReportNotFound()
{
	beep();
	FindState::ReportStatus("Not found", false);
}


static void ReportError(const std::string &message)
{
	beep();
	FindState::ReportStatus(message, true);
}


static bool HasSelection(CodeEditView *view)
{
	const Selection selection = view->GetSelection();
	return selection.anchor != selection.caret;
}


static std::string SelectedText(CodeEditView *view)
{
	const Selection selection = view->GetSelection();
	const size_t start = std::min(selection.anchor, selection.caret);
	const size_t end = std::max(selection.anchor, selection.caret);
	TextModel &model = *view->Model();
	ModelLock lock(model);
	return model.Text().Substring(start, end - start);
}


// The text as it is now, to search without the model lock.
static CodeEdit::Rope TextOf(CodeEditView *view)
{
	TextModel &model = *view->Model();
	ModelLock lock(model);
	return model.Text();
}


// Empty if the pattern is empty or invalid; reported if report is set.
static std::optional<Searcher> MakeSearcher(const FindSettings &settings, bool report)
{
	if (settings.findText.empty()) {
		if (report) {
			beep();
		}
		return std::nullopt;
	}
	try {
		return Searcher(NormalizeLineEndings(settings.findText), settings.regex, settings.ignoreCase);
	} catch (const PatternError &error) {
		if (report) {
			ReportError("Invalid pattern at " + std::to_string(error.Offset()) + ": " + error.what());
		}
		return std::nullopt;
	}
}


static bool ExecuteFind(CodeEditView *view, const FindSettings &settings, bool backward)
{
	const std::optional<Searcher> searcher = MakeSearcher(settings, true);
	if (!searcher.has_value()) {
		return false;
	}
	const Selection selection = view->GetSelection();
	std::optional<Match> match;
	try {
		const CodeEdit::Rope text = TextOf(view);
		match = FindNext(*searcher, RopeSource(text), selection, backward, settings.wrap);
	} catch (const SearchError &error) {
		ReportError(error.what());
		return false;
	}
	if (!match.has_value()) {
		ReportNotFound();
		return false;
	}

	const bool wrapped = backward
		? match->offset >= std::min(selection.anchor, selection.caret)
		: match->offset < std::max(selection.anchor, selection.caret);
	view->SetSelection({match->offset, match->End()});
	view->ScrollToSelection();
	FindState::ReportStatus(wrapped ? "Wrapped around" : "", false);
	return true;
}


static bool ExecuteReplace(CodeEditView *view, const FindSettings &settings)
{
	if (view->Settings().readOnly) {
		beep();
		return false;
	}
	const std::optional<Searcher> searcher = MakeSearcher(settings, true);
	if (!searcher.has_value()) {
		return false;
	}
	std::optional<Selection> after;
	try {
		after = ReplaceSelection(
			*view->Model(),
			view->EditOwner(),
			*searcher,
			NormalizeLineEndings(settings.replaceText),
			view->GetSelection()
		);
	} catch (const SearchError &error) {
		ReportError(error.what());
		return false;
	}
	if (!after.has_value()) {
		beep();
		FindState::ReportStatus("The selection is not a match", false);
		return false;
	}
	view->SetSelection(*after);
	view->ScrollToSelection();
	FindState::ReportStatus("", false);
	return true;
}


static void ExecuteReplaceAll(CodeEditView *view, const FindSettings &settings)
{
	if (view->Settings().readOnly) {
		beep();
		return;
	}
	const std::optional<Searcher> searcher = MakeSearcher(settings, true);
	if (!searcher.has_value()) {
		return;
	}
	const Selection selection = view->GetSelection();
	if (settings.inSelection && selection.anchor == selection.caret) {
		beep();
		FindState::ReportStatus("No selection", false);
		return;
	}
	ReplaceAllResult result;
	try {
		result = ReplaceAll(
			*view->Model(),
			view->EditOwner(),
			*searcher,
			NormalizeLineEndings(settings.replaceText),
			selection,
			settings.inSelection
		);
	} catch (const SearchError &error) {
		ReportError(error.what());
		return;
	}
	if (result.count == 0) {
		ReportNotFound();
		return;
	}
	view->SetSelection(result.selection);
	view->ScrollToSelection();
	FindState::ReportStatus(
		"Replaced " + std::to_string(result.count) + (result.count == 1 ? " occurrence" : " occurrences"),
		false
	);
}


static bool SelectionIsMatch(CodeEditView *view, const FindSettings &settings)
{
	if (!HasSelection(view)) {
		return false;
	}
	const std::optional<Searcher> searcher = MakeSearcher(settings, false);
	if (!searcher.has_value()) {
		return false;
	}
	const Selection selection = view->GetSelection();
	try {
		const CodeEdit::Rope text = TextOf(view);
		return SelectionMatch(*searcher, RopeSource(text), selection).has_value();
	} catch (const SearchError &error) {
		return false;
	}
}


class FindCommandSet: public EditorCommandSet {
public:
	bool Handles(uint32 what) const final;
	bool IsEnabled(uint32 what, CodeEditView *view) final;
	void Execute(BMessage *message, CodeEditView *view) final;
};


bool FindCommandSet::Handles(uint32 what) const
{
	switch (what) {
		case kMsgFind:
		case kMsgFindAgain:
		case kMsgFindAgainBackward:
		case kMsgFindSelection:
		case kMsgFindSelectionBackward:
		case kMsgEnterSearchString:
		case kMsgReplace:
		case kMsgReplaceAndFind:
		case kMsgReplaceAll:
		case kMsgGoToLine:
		case CodeEdit::kMsgPositionClicked:
			return true;
		default:
			return false;
	}
}


bool FindCommandSet::IsEnabled(uint32 what, CodeEditView *view)
{
	if (what == kMsgFind) {
		return true;
	}
	if (view == nullptr) {
		return false;
	}
	const FindSettings settings = FindState::Get();
	const bool editable = !view->Settings().readOnly;
	switch (what) {
		case kMsgFindAgain:
		case kMsgFindAgainBackward:
			return !settings.findText.empty();
		case kMsgFindSelection:
		case kMsgFindSelectionBackward:
		case kMsgEnterSearchString:
			return HasSelection(view);
		case kMsgReplace:
			return editable;
		case kMsgReplaceAndFind:
			return editable && SelectionIsMatch(view, settings);
		case kMsgReplaceAll:
			return editable && !settings.findText.empty() && (!settings.inSelection || HasSelection(view));
		case kMsgGoToLine:
		case CodeEdit::kMsgPositionClicked:
			return true;
		default:
			return false;
	}
}


void FindCommandSet::Execute(BMessage *message, CodeEditView *view)
{
	if (message->what == kMsgFind) {
		FindPanel::ShowPanel();
		return;
	}
	if (view == nullptr) {
		beep();
		return;
	}

	if (message->what == kMsgGoToLine || message->what == CodeEdit::kMsgPositionClicked) {
		int32 line = 0;
		if (message->FindInt32("line", &line) != B_OK) {
			(new GoToLinePanel(view))->Show();
			return;
		}
		const int32 column = message->GetInt32("column", 1);
		view->GoToLocation({static_cast<size_t>(std::max<int32>(line, 1) - 1),
			static_cast<size_t>(std::max<int32>(column, 1) - 1)});
		return;
	}

	FindSettings settings = FindState::Get();
	switch (message->what) {
		case kMsgFindAgain:
		case kMsgFindAgainBackward:
			ExecuteFind(view, settings, settings.backward != (message->what == kMsgFindAgainBackward));
			break;
		case kMsgFindSelection:
		case kMsgFindSelectionBackward:
		case kMsgEnterSearchString: {
			const std::string text = SelectedText(view);
			if (text.empty()) {
				beep();
				break;
			}
			settings.findText = text;
			settings.regex = false;
			FindState::Set(settings);
			if (message->what != kMsgEnterSearchString) {
				ExecuteFind(view, settings, settings.backward != (message->what == kMsgFindSelectionBackward));
			}
			break;
		}
		case kMsgReplace:
			if (!ExecuteReplace(view, settings)) {
				FindPanel::ShowPanel();
			}
			break;
		case kMsgReplaceAndFind:
			if (ExecuteReplace(view, settings)) {
				ExecuteFind(view, settings, settings.backward);
			}
			break;
		case kMsgReplaceAll:
			ExecuteReplaceAll(view, settings);
			break;
		default:
			break;
	}
}


void AddFindItems(BMenu *menu)
{
	static FindCommandSet sCommands;
	CodeEdit::EditorCommands::Register(&sCommands);

	menu->AddItem(new BMenuItem("Find" B_UTF8_ELLIPSIS, new BMessage(kMsgFind), 'F'));
	menu->AddItem(new BMenuItem("Find again", new BMessage(kMsgFindAgain), 'G'));
	menu->AddItem(new BMenuItem("Find again backwards", new BMessage(kMsgFindAgainBackward), 'G', B_SHIFT_KEY));
	menu->AddItem(new BMenuItem("Find selection", new BMessage(kMsgFindSelection), 'H'));
	menu->AddItem(new BMenuItem("Find selection backwards", new BMessage(kMsgFindSelectionBackward), 'H', B_SHIFT_KEY));
	menu->AddItem(new BMenuItem("Enter search string", new BMessage(kMsgEnterSearchString), 'E'));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Replace", new BMessage(kMsgReplace), '='));
	menu->AddItem(new BMenuItem("Replace & find", new BMessage(kMsgReplaceAndFind), 'T'));
	menu->AddItem(new BMenuItem("Replace all", new BMessage(kMsgReplaceAll)));
	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem("Go to line" B_UTF8_ELLIPSIS, new BMessage(kMsgGoToLine), 'L'));
}


} // FindKit
