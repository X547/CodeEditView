#pragma once

#include <SupportDefs.h>


namespace DirCompare {


// Application.
// Shows the open window.
constexpr uint32 kMsgNewComparison = 'DCnw';
// "left", "right" (strings): paths to compare.
constexpr uint32 kMsgOpenComparison = 'DCop';
constexpr uint32 kMsgWindowClosed = 'DCwc';
constexpr uint32 kMsgQuitApplication = 'DCqu';

// Open window.
// "pane" (int32).
constexpr uint32 kMsgBrowse = 'DCbr';
constexpr uint32 kMsgSwapPaths = 'DCsp';
constexpr uint32 kMsgCompare = 'DCcm';

// Comparison window: menus.
constexpr uint32 kMsgRefresh = 'DCrf';
constexpr uint32 kMsgSave = 'DCsv';
constexpr uint32 kMsgSaveAll = 'DCsa';
constexpr uint32 kMsgReload = 'DCrl';
// "pane" (int32): the pane to copy to.
constexpr uint32 kMsgCopyChanges = 'DCcc';
constexpr uint32 kMsgNextChange = 'DCnc';
constexpr uint32 kMsgPreviousChange = 'DCpc';
constexpr uint32 kMsgNextFile = 'DCnf';
constexpr uint32 kMsgPreviousFile = 'DCpf';
// "pane" (int32): the side to copy to.
constexpr uint32 kMsgCopyEntry = 'DCce';
// "pane" (int32): the side to delete from.
constexpr uint32 kMsgDeleteEntry = 'DCde';
// "pane" (int32).
constexpr uint32 kMsgShowInTracker = 'DCtr';
constexpr uint32 kMsgSwapSides = 'DCsw';
constexpr uint32 kMsgToggleShowIdentical = 'DCsi';
constexpr uint32 kMsgToggleIgnoreVcs = 'DCiv';
constexpr uint32 kMsgToggleSyncScrolling = 'DCss';
constexpr uint32 kMsgToggleHideUnchanged = 'DChu';
constexpr uint32 kMsgToggleWordWrap = 'DCww';
// "algorithm" (int32): DiffAlgorithm.
constexpr uint32 kMsgSetAlgorithm = 'DCal';

// Directory tree.
constexpr uint32 kMsgEntrySelected = 'DCes';
constexpr uint32 kMsgEntryInvoked = 'DCei';


} // DirCompare
