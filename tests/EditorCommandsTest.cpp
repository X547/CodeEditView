#include <catch2/catch_test_macros.hpp>

#include <AppDefs.h>
#include <MenuItem.h>
#include <Message.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/EditorCommands.h>


namespace CodeEdit {


static constexpr uint32 kMsgTestCommand = 'Ttc1';
static constexpr uint32 kMsgOtherCommand = 'Ttc2';


class TestCommandSet: public EditorCommandSet {
public:
	int32 executed {};
	bool hadView = true;

	bool Handles(uint32 what) const final {return what == kMsgTestCommand;}

	bool IsEnabled(uint32 what, CodeEditView *view) final
	{
		(void)what;
		return view != nullptr;
	}

	void Execute(BMessage *message, CodeEditView *view) final
	{
		(void)message;
		executed++;
		hadView = view != nullptr;
	}
};


TEST_CASE("editor commands are dispatched to the registered set")
{
	TestCommandSet set;
	BMessage command(kMsgTestCommand);
	BMessage other(kMsgOtherCommand);

	CHECK_FALSE(EditorCommands::Dispatch(&command, nullptr));

	EditorCommands::Register(&set);
	EditorCommands::Register(&set);
	CHECK(EditorCommands::Dispatch(&command, nullptr));
	CHECK(set.executed == 1);
	CHECK_FALSE(set.hadView);
	CHECK_FALSE(EditorCommands::Dispatch(&other, nullptr));
	CHECK(set.executed == 1);

	EditorCommands::Unregister(&set);
	CHECK_FALSE(EditorCommands::Dispatch(&command, nullptr));
	CHECK(set.executed == 1);
}


TEST_CASE("undo and clipboard commands are built in")
{
	BMessage undo(B_UNDO);
	BMessage copy(B_COPY);
	CHECK(EditorCommands::Dispatch(&undo, nullptr));
	CHECK(EditorCommands::Dispatch(&copy, nullptr));
}


TEST_CASE("shift, folding and bookmark commands are built in")
{
	for (const uint32 what : {kMsgIndentLess, kMsgFoldAll, kMsgToggleBookmark, kMsgNextBookmark,
		kMsgClearBookmarks}) {
		BMessage message(what);
		CHECK(EditorCommands::Dispatch(&message, nullptr));
	}
}


TEST_CASE("menu items are enabled as their command set says")
{
	TestCommandSet set;
	BMenuItem item("Test", new BMessage(kMsgTestCommand));
	set.UpdateItem(item, nullptr);
	CHECK_FALSE(item.IsEnabled());
}


TEST_CASE("there is no focused editor without a window")
{
	CHECK(EditorCommands::FocusedEditor(nullptr) == nullptr);
	CHECK_FALSE(EditorCommands::ActiveHost().IsValid());
}


} // CodeEdit
