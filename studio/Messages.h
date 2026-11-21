#pragma once

#include <SupportDefs.h>


namespace Studio {


// "locator" (string): show the document. A source window receiving it opens
// the window in its own stack; the application opens a separate window.
// Optional: "newView" (bool), "session" (int32: the session the new window
// follows), "line" (int32, 1-based: revealed).
constexpr uint32 kMsgOpenLocator = 'CSol';
// To a source window: activates it; "line" (int32, 1-based) to reveal, or
// with "goTo" (bool) to move the caret to, with "column" (int32, 1-based).
constexpr uint32 kMsgReveal = 'CSrv';
// "session" (int32): to the application, from the Debug menu or a start
// window; to the source windows after the session closed.
constexpr uint32 kMsgCloseSession = 'CScs';
constexpr uint32 kMsgSessionClosed = 'CSsc';
// To a source window: "session" (int32; 0 for none) to follow.
constexpr uint32 kMsgFollowSession = 'SWfs';
// To the application: "session" (int32) followed by the active source
// window; the debug tool windows show it (0 leaves them as they are).
constexpr uint32 kMsgActiveSession = 'CSas';
// To the source windows: "session" (int32) has another status.
constexpr uint32 kMsgSessionStatusChanged = 'CSss';
// "coverage" (int32), "locator" (string): shows the document in a window
// following the coverage run.
constexpr uint32 kMsgShowCoverageSource = 'CScS';
// "coverage" (int32): to the application, from the run's summary window;
// to the source windows after the run closed.
constexpr uint32 kMsgCloseCoverage = 'CScc';
constexpr uint32 kMsgCoverageClosed = 'CSCc';
// "coverage" (int32): shows the run's summary window.
constexpr uint32 kMsgShowCoverageSummary = 'CSCs';
// Asks for a binary and a profile to open a coverage run.
constexpr uint32 kMsgShowOpenCoverage = 'CSoc';
// "directory" (string, optional): where the dialog starts.
constexpr uint32 kMsgShowOpenFile = 'CSof';
// Opens a new untitled document.
constexpr uint32 kMsgNewDocument = 'CSnd';
// "locator" (string, optional): a directory of a Git commit to start at.
constexpr uint32 kMsgShowOpenGit = 'CSog';
constexpr uint32 kMsgShowOpenLocation = 'CSoL';
constexpr uint32 kMsgShowDocuments = 'CSsd';
// To a source window: shows the file options dialog of its document.
constexpr uint32 kMsgShowFileOptions = 'CSfo';
// From the file options dialog to its source window: "mimeType" (string,
// empty for none), "language" (string, empty for plain text; missing:
// automatic), "tabWidth", "indentSize" (int32; 0: the tab width),
// "insertSpaces" (bool), "lineEnding" (int32: CodeEdit::LineEnding), "bom"
// (bool).
constexpr uint32 kMsgFileOptionsChosen = 'CSfc';
constexpr uint32 kMsgToggleStacking = 'CSts';
// To the application after a window closed.
constexpr uint32 kMsgWindowClosed = 'CSwc';
// To a source window.
constexpr uint32 kMsgActivate = 'CSac';


} // Studio
