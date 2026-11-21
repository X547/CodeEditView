#include <CodeEditView/TextCommands.h>

#include <Beep.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>


namespace CodeEdit {


namespace TextCommands {


using Command = std::optional<Selection> (*)(
	TextModel &model,
	ObserverId owner,
	const Selection &selection,
	const EditorSettings &settings
);


class TextCommandSet: public EditorCommandSet {
private:
	static Command CommandOf(uint32 what);

public:
	bool Handles(uint32 what) const final;
	bool IsEnabled(uint32 what, CodeEditView *view) final;
	void Execute(BMessage *message, CodeEditView *view) final;
};


Command TextCommandSet::CommandOf(uint32 what)
{
	switch (what) {
		case kMsgCleanupWhitespace:
			return CleanupWhitespace;
		case kMsgTabsToSpaces:
			return TabsToSpaces;
		case kMsgLeadingSpacesToTabs:
			return LeadingSpacesToTabs;
		default:
			break;
	}
	return nullptr;
}


bool TextCommandSet::Handles(uint32 what) const
{
	return CommandOf(what) != nullptr;
}


bool TextCommandSet::IsEnabled(uint32 what, CodeEditView *view)
{
	(void)what;
	return view != nullptr && !view->Settings().readOnly;
}


void TextCommandSet::Execute(BMessage *message, CodeEditView *view)
{
	if (view == nullptr || view->Settings().readOnly) {
		beep();
		return;
	}
	const std::optional<Selection> after = CommandOf(message->what)(
		*view->Model(),
		view->EditOwner(),
		view->GetSelection(),
		view->Settings()
	);
	if (after.has_value()) {
		view->SetSelection(*after);
	}
}


void AddItems(BMenu *menu)
{
	static TextCommandSet sCommands;
	EditorCommands::Register(&sCommands);

	menu->AddItem(new BMenuItem("Clean up whitespace", new BMessage(kMsgCleanupWhitespace)));
	menu->AddItem(new BMenuItem("Convert tabs to spaces", new BMessage(kMsgTabsToSpaces)));
	menu->AddItem(new BMenuItem("Convert leading spaces to tabs", new BMessage(kMsgLeadingSpacesToTabs)));
}


} // TextCommands


} // CodeEdit
