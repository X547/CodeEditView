#pragma once

#include <SupportDefs.h>


namespace GitTool {


// Application and main window.
constexpr uint32 kMsgOpenRepository = 'GTor';
constexpr uint32 kMsgMainWindowClosed = 'GTwc';
constexpr uint32 kMsgQuitApplication = 'GTqu';
constexpr uint32 kMsgRefresh = 'GTrf';
constexpr uint32 kMsgRefreshTimer = 'GTrt';
constexpr uint32 kMsgCategorySelected = 'GTcs';
constexpr uint32 kMsgObjectSelected = 'GTos';
constexpr uint32 kMsgObjectInvoked = 'GToi';
constexpr uint32 kMsgContextMenu = 'GTcm';
// "action" (int32): Action.
constexpr uint32 kMsgAction = 'GTac';
constexpr uint32 kMsgCancelNetwork = 'GTcn';

// Network task to the main window. "text" (string).
constexpr uint32 kMsgNetworkProgress = 'GTnp';
constexpr uint32 kMsgNetworkLog = 'GTnl';
// "error" (string, empty on success); the fields of the follow-up message.
constexpr uint32 kMsgNetworkDone = 'GTnd';

// Dialogs.
constexpr uint32 kMsgDialogOk = 'GTok';
constexpr uint32 kMsgDialogCancel = 'GTca';
// "field" (int32).
constexpr uint32 kMsgPickCommit = 'GTpc';
// "resolution" (int32).
constexpr uint32 kMsgResolution = 'GTre';
constexpr uint32 kMsgConfirmToggled = 'GTct';
// "spec" (string).
constexpr uint32 kMsgHistoryOf = 'GThi';
constexpr uint32 kMsgCommitSelected = 'GTcl';
constexpr uint32 kMsgCommitInvoked = 'GTci';
constexpr uint32 kMsgShowCommit = 'GTsh';

// Commit window.
constexpr uint32 kMsgCommitFileSelected = 'GTcf';
// "parent" (int32).
constexpr uint32 kMsgSetParent = 'GTsp';
constexpr uint32 kMsgNextChange = 'GTnx';
constexpr uint32 kMsgPreviousChange = 'GTpv';
constexpr uint32 kMsgToggleSyncScrolling = 'GTss';
constexpr uint32 kMsgToggleHideUnchanged = 'GThu';
constexpr uint32 kMsgToggleWordWrap = 'GTww';

// History window.
constexpr uint32 kMsgEditMode = 'GTem';
constexpr uint32 kMsgRemoveSteps = 'GTrs';
constexpr uint32 kMsgCombineStep = 'GTcb';
constexpr uint32 kMsgRewordStep = 'GTrw';
constexpr uint32 kMsgResetEdits = 'GTrz';
constexpr uint32 kMsgApplyEdits = 'GTap';
// Dragged commits: "commit" (raw git_oid, repeated), "source"
// (messenger of the window), "step" (int32, repeated, in edit mode); when
// dropped, "index" (int32): the row the commits go above.
constexpr uint32 kMsgDragCommits = 'GTdg';
// StatsLoader: "commit" (raw git_oid).
constexpr uint32 kMsgStatsReady = 'GTsr';
constexpr uint32 kMsgEditContents = 'GTec';
constexpr uint32 kMsgSplitStep = 'GTsl';
constexpr uint32 kMsgResolveStep = 'GTrv';

// Step editor.
// To the history window; see StepEditor.
constexpr uint32 kMsgStepEdited = 'GTsd';
constexpr uint32 kMsgStepFileSelected = 'GTsf';
// "version" (int32).
constexpr uint32 kMsgLeftVersion = 'GTlv';
constexpr uint32 kMsgDeleteToggled = 'GTdt';
constexpr uint32 kMsgTakeLeft = 'GTtl';
constexpr uint32 kMsgAddFile = 'GTaf';


} // GitTool
