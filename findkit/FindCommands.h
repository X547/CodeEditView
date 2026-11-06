#pragma once

#include <SupportDefs.h>


class BMenu;


namespace FindKit {


// Find commands, executed on the target editor of a host window (see
// CodeEdit::EditorCommands). They use the shared FindState.
constexpr uint32 kMsgFind = 'FKfd';
constexpr uint32 kMsgFindAgain = 'FKfa';
constexpr uint32 kMsgFindAgainBackward = 'FKfb';
constexpr uint32 kMsgFindSelection = 'FKfs';
constexpr uint32 kMsgFindSelectionBackward = 'FKfS';
constexpr uint32 kMsgEnterSearchString = 'FKes';
constexpr uint32 kMsgReplace = 'FKrp';
constexpr uint32 kMsgReplaceAndFind = 'FKrf';
constexpr uint32 kMsgReplaceAll = 'FKra';


// Adds the find and replace items to the menu and registers the find
// commands with CodeEdit::EditorCommands.
void AddFindItems(BMenu *menu);


} // FindKit
