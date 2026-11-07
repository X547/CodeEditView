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


} // GitTool
