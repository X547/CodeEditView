#pragma once

#include <SupportDefs.h>


namespace Studio {


// "locator" (string): show the document. A source window receiving it opens
// the window in its own stack; the application opens a separate window.
constexpr uint32 kMsgOpenLocator = 'CSol';
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
