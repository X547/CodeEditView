#pragma once

#include <SupportDefs.h>


namespace Debugger {


// Commands to the DebugSession.
// "program" (string), "arguments" (string, split at blanks), "stopAtMain"
// (bool).
constexpr uint32 kMsgLaunch = 'DSla';
// "team" (int32).
constexpr uint32 kMsgAttach = 'DSat';
constexpr uint32 kMsgContinue = 'DScn';
constexpr uint32 kMsgPause = 'DSps';
constexpr uint32 kMsgStepOver = 'DSso';
constexpr uint32 kMsgStepInto = 'DSsi';
constexpr uint32 kMsgStepOut = 'DSsu';
// Ends a launched program, detaches from an attached one.
constexpr uint32 kMsgStop = 'DSst';
// "thread" (int64).
constexpr uint32 kMsgSelectThread = 'DSth';
// "frame" (int32): index in the current stack; "show" (bool): show its
// source (default).
constexpr uint32 kMsgSelectFrame = 'DSfr';
// "reference" (int64): children of a variable or scope.
constexpr uint32 kMsgRequestVariables = 'DSva';
// "expression" (string): a GDB command or expression from the console.
constexpr uint32 kMsgEvaluate = 'DSev';
// "target" (messenger): receives the notices below, starting with the
// current state.
constexpr uint32 kMsgWatch = 'DSwa';


// Notices from the DebugSession.
enum class SessionState: int32 {
	Idle,
	Starting,
	Running,
	Stopped,
};

// "state" (int32 SessionState), "text" (string): status line.
constexpr uint32 kNoticeState = 'DNst';
// "text" (string), "category" (string): program and debugger output.
constexpr uint32 kNoticeOutput = 'DNou';
// "id" (int64[]), "name" (string[]), "current" (int64).
constexpr uint32 kNoticeThreads = 'DNth';
// "function", "module", "path" (string[]), "line" (int32[], 1-based, 0 if
// none), "current" (int32): frames of the current thread.
constexpr uint32 kNoticeStack = 'DNsk';
// "name" (string[]), "reference" (int64[]): scopes of the current frame.
constexpr uint32 kNoticeScopes = 'DNsc';
// "reference" (int64): parent; "name", "value", "type" (string[]),
// "child" (int64[], 0 without children).
constexpr uint32 kNoticeVariables = 'DNva';
// "path" (string[]), "line" (int32[], 1-based), "enabled", "verified"
// (bool[]), "message" (string[]).
constexpr uint32 kNoticeBreakpoints = 'DNbp';


// Application messages.
// "locator" (string), optional "line" (int32, 1-based), "session" (int32
// session id): show a source window following the session.
constexpr uint32 kMsgShowSource = 'DAss';
// "window" (int32 ToolWindowKind), "session" (int32 session id).
constexpr uint32 kMsgShowToolWindow = 'DAtw';
// "locator" (string).
constexpr uint32 kMsgSourceWindowClosed = 'DAsc';

enum class ToolWindowKind: int32 {
	Console,
	Threads,
	Stack,
	Variables,
	Breakpoints,
};


// Mark kind of the selected caller frame (a host-defined kind).
constexpr uint32_t kMarkFrameLine = 1000;


} // Debugger
