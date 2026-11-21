#include <CodeEditView/EditorCommands.h>

#include <algorithm>
#include <vector>

#include <Autolock.h>
#include <Locker.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Message.h>
#include <Window.h>

#include <CodeEditView/CodeEditView.h>


namespace CodeEdit {


void EditorCommandSet::UpdateItem(BMenuItem &item, CodeEditView *view)
{
	item.SetEnabled(IsEnabled(item.Message()->what, view));
}


namespace EditorCommands {


static BLocker sLock("editor commands");
static std::vector<EditorCommandSet*> sSets;
static BMessenger sActiveHost;


static EditorCommandSet *FindSet(uint32 what)
{
	BAutolock lock(sLock);
	for (EditorCommandSet *set: sSets) {
		if (set->Handles(what)) {
			return set;
		}
	}
	return nullptr;
}


void Register(EditorCommandSet *set)
{
	BAutolock lock(sLock);
	if (std::find(sSets.begin(), sSets.end(), set) == sSets.end()) {
		sSets.push_back(set);
	}
}


void Unregister(EditorCommandSet *set)
{
	BAutolock lock(sLock);
	sSets.erase(std::remove(sSets.begin(), sSets.end(), set), sSets.end());
}


CodeEditView *FocusedEditor(BWindow *window)
{
	if (window == nullptr) {
		return nullptr;
	}
	return dynamic_cast<CodeEditView*>(window->CurrentFocus());
}


void SetActiveHost(BWindow *window)
{
	BAutolock lock(sLock);
	sActiveHost = BMessenger(window);
}


BMessenger ActiveHost()
{
	BAutolock lock(sLock);
	return sActiveHost;
}


void UpdateMenu(BMenu *menu, CodeEditView *view)
{
	if (menu == nullptr) {
		return;
	}
	for (int32 i = 0; i < menu->CountItems(); i++) {
		BMenuItem *item = menu->ItemAt(i);
		if (item->Submenu() != nullptr) {
			UpdateMenu(item->Submenu(), view);
			continue;
		}
		if (item->Message() == nullptr) {
			continue;
		}
		EditorCommandSet *set = FindSet(item->Message()->what);
		if (set != nullptr) {
			set->UpdateItem(*item, view);
		}
	}
}


bool Dispatch(BMessage *message, CodeEditView *view)
{
	EditorCommandSet *set = FindSet(message->what);
	if (set == nullptr) {
		return false;
	}
	set->Execute(message, view);
	return true;
}


} // EditorCommands


} // CodeEdit
