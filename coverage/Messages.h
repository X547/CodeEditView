#pragma once

#include <SupportDefs.h>


namespace Coverage {


// Application: "binary", "profile" (strings).
constexpr uint32 kMsgOpenCoverage = 'COop';
constexpr uint32 kMsgShowOpenWindow = 'COow';
constexpr uint32 kMsgWindowClosed = 'COwc';

// Open window.
constexpr uint32 kMsgBrowse = 'CObr';
constexpr uint32 kMsgOpen = 'COoo';

// Coverage window.
constexpr uint32 kMsgReload = 'COrl';
constexpr uint32 kMsgFileSelected = 'COfs';
constexpr uint32 kMsgNextUncovered = 'COnu';
constexpr uint32 kMsgPreviousUncovered = 'COpu';

// Worker requests and replies. Replies carry "result" (pointer, owned by
// the receiver) or "error" (string).
constexpr uint32 kMsgLoadSummary = 'COls';
constexpr uint32 kMsgSummaryLoaded = 'COsl';
// "path" (string), "request" (uint64).
constexpr uint32 kMsgLoadFile = 'COlf';
constexpr uint32 kMsgFileLoaded = 'COfl';


} // Coverage
