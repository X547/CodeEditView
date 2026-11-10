#pragma once


namespace DiffKit {


// Reports loopers that stop responding: a thread pings the application and
// each window; when one does not answer within the threshold, the stacks of
// all threads are saved (with gdb) to
// ~/config/settings/CodeEdit/freezes/<app>-<time>.txt, and the report gets
// the length of the freeze once the looper answers again. Nothing is
// exempt: a window thread waiting for a synchronous dialog is frozen too.
//
// The environment variable CODEEDIT_FREEZE_MS sets the threshold in
// milliseconds (default 500); 0 turns the watchdog off. Call once after the
// application object exists; the thread lives as long as the application.
void StartFreezeWatchdog(const char *appName);


} // DiffKit
