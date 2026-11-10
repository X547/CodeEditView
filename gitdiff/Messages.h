#pragma once

#include <SupportDefs.h>


namespace GitDiff {


// Window and menu commands.
constexpr uint32 kMsgOpenRepository = 'GDor';
constexpr uint32 kMsgRefresh = 'GDrf';
constexpr uint32 kMsgSave = 'GDsv';
constexpr uint32 kMsgSaveAll = 'GDsa';
constexpr uint32 kMsgReload = 'GDrl';
constexpr uint32 kMsgStage = 'GDst';
constexpr uint32 kMsgRevert = 'GDrv';
constexpr uint32 kMsgStageFile = 'GDsf';
constexpr uint32 kMsgCommit = 'GDcm';
constexpr uint32 kMsgAmendToggled = 'GDam';
constexpr uint32 kMsgChangeIdToggled = 'GDci';
constexpr uint32 kMsgNextChunk = 'GDnc';
constexpr uint32 kMsgPreviousChunk = 'GDpc';
constexpr uint32 kMsgToggleSyncScrolling = 'GDss';
constexpr uint32 kMsgToggleHideUnchanged = 'GDhu';
constexpr uint32 kMsgToggleWordWrap = 'GDww';
// "algorithm" (int32): DiffAlgorithm.
constexpr uint32 kMsgSetAlgorithm = 'GDal';
constexpr uint32 kMsgQuitApplication = 'GDqu';

// File list.
constexpr uint32 kMsgFileSelected = 'GDfs';
constexpr uint32 kMsgFileInvoked = 'GDfi';
// "path" (string): a file chosen in the path menu, relative to the working
// tree.
constexpr uint32 kMsgShowPath = 'GDsp';

// Internal notifications.
// The index or a working tree file was written; the status may differ.
constexpr uint32 kMsgStatusChanged = 'GDsc';
constexpr uint32 kMsgRefreshTimer = 'GDrt';
constexpr uint32 kMsgWindowClosed = 'GDwc';


} // GitDiff
