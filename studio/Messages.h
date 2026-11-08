#pragma once

#include <SupportDefs.h>


namespace Studio {


// "locator" (string): show the document. A source window receiving it opens
// the window in its own stack; the application opens a separate window.
// Optional: "newView" (bool), "session" (int32: the session the new window
// follows), "line" (int32, 1-based: revealed).
constexpr uint32 kMsgOpenLocator = 'CSol';
// To a source window: "line" (int32, 1-based) to reveal; activates it.
constexpr uint32 kMsgReveal = 'CSrv';
// "session" (int32): to the application, from a session's control window;
// to the source windows after the session closed.
constexpr uint32 kMsgCloseSession = 'CScs';
constexpr uint32 kMsgSessionClosed = 'CSsc';
// "session" (int32): shows the session's control window.
constexpr uint32 kMsgShowSessionControl = 'CSsC';
// "directory" (string, optional): where the dialog starts.
constexpr uint32 kMsgShowOpenFile = 'CSof';
constexpr uint32 kMsgShowOpenGit = 'CSog';
constexpr uint32 kMsgShowOpenLocation = 'CSoL';
constexpr uint32 kMsgShowDocuments = 'CSsd';
constexpr uint32 kMsgToggleStacking = 'CSts';
// To the application after a window closed.
constexpr uint32 kMsgWindowClosed = 'CSwc';
// To a source window.
constexpr uint32 kMsgActivate = 'CSac';


} // Studio
