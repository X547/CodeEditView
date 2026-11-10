#include "FreezeWatchdog.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <Application.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Messenger.h>
#include <OS.h>
#include <Path.h>
#include <Window.h>


namespace DiffKit {


// Not handled by any looper; the default handling replies to it.
static const uint32 kMsgPing = 'FWpg';
// How often the loopers are checked.
static const bigtime_t kInterval = 250000;
// Stacks saved for one freeze at most.
static const int kMaxSamples = 5;


struct WatchedLooper {
	BMessenger messenger;
	thread_id thread = -1;
	std::string name;
};


struct Watchdog {
	std::string appName;
	bigtime_t threshold {};
};


static std::string ThreadName(thread_id thread)
{
	thread_info info;
	return get_thread_info(thread, &info) == B_OK ? info.name : "thread " + std::to_string(thread);
}


// The application and its windows, read quickly: a window pointer is only
// used right after it was returned.
static std::vector<WatchedLooper> Loopers()
{
	std::vector<WatchedLooper> loopers;
	loopers.push_back({BMessenger(be_app), be_app->Thread(), "application"});
	for (int32 i = 0; BWindow *window = be_app->WindowAt(i); i++) {
		// A window that is not shown yet has no thread to answer.
		const thread_id thread = window->Thread();
		if (thread >= 0) {
			loopers.push_back({BMessenger(window), thread, ThreadName(thread)});
		}
	}
	return loopers;
}


// Whether the looper answers within the timeout.
static bool Answers(const BMessenger &messenger, bigtime_t timeout)
{
	BMessage ping(kMsgPing);
	BMessage reply;
	const status_t status = messenger.SendMessage(&ping, &reply, 100000, timeout);
	// A looper that is gone does not freeze; only a missing answer counts.
	return status != B_TIMED_OUT && status != B_WOULD_BLOCK;
}


static std::string ReportPath(const std::string &appName)
{
	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) != B_OK) {
		return std::string();
	}
	path.Append("CodeEdit/freezes");
	create_directory(path.Path(), 0755);
	char stamp[32];
	const time_t now = time(nullptr);
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
	return std::string(path.Path()) + "/" + appName + "-" + stamp + "-" + std::to_string(system_time() % 1000) + ".txt";
}


// The stacks of all threads of this team, from gdb.
static std::string Stacks()
{
	const std::string command = "gdb -p " + std::to_string(getpid())
		+ " -batch -nx -ex 'thread apply all bt' 2>&1";
	FILE *pipe = popen(command.c_str(), "r");
	if (pipe == nullptr) {
		return "(cannot run gdb)\n";
	}
	std::string text;
	char buffer[4096];
	size_t count;
	while ((count = fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
		text.append(buffer, count);
	}
	pclose(pipe);
	return text;
}


static void Report(const Watchdog &watchdog, const WatchedLooper &looper, bigtime_t start)
{
	const std::string path = ReportPath(watchdog.appName);
	fprintf(stderr, "%s: %s (thread %" B_PRId32 ") does not respond; report: %s\n", watchdog.appName.c_str(),
		looper.name.c_str(), looper.thread, path.c_str());
	std::string text = "Freeze of " + looper.name + " (thread " + std::to_string(looper.thread) + ") in "
		+ watchdog.appName + ": no answer within " + std::to_string(watchdog.threshold / 1000) + " ms.\n\n";
	// A long freeze is sampled a few times, as it may go through several
	// slow steps.
	text += "Sample 1:\n" + Stacks();
	int samples = 1;
	while (!Answers(looper.messenger, 1500000) && looper.messenger.IsValid()) {
		if (samples < kMaxSamples) {
			samples++;
			char title[64];
			snprintf(title, sizeof(title), "\nSample %d, after %.1f s:\n", samples, (system_time() - start) / 1e6);
			text += title + Stacks();
		}
	}
	const bigtime_t length = system_time() - start;
	char line[96];
	snprintf(line, sizeof(line), "\nThe freeze lasted %.1f s.\n", length / 1e6);
	text = line + text;
	fprintf(stderr, "%s: %s responds again after %.1f s\n", watchdog.appName.c_str(), looper.name.c_str(),
		length / 1e6);

	if (FILE *file = path.empty() ? nullptr : fopen(path.c_str(), "w")) {
		fwrite(text.data(), 1, text.size(), file);
		fclose(file);
	}
}


static status_t Run(void *data)
{
	Watchdog *watchdog = static_cast<Watchdog*>(data);
	while (true) {
		snooze(kInterval);
		if (be_app == nullptr) {
			continue;
		}
		for (const WatchedLooper &looper : Loopers()) {
			const bigtime_t start = system_time();
			if (!Answers(looper.messenger, watchdog->threshold)) {
				Report(*watchdog, looper, start);
			}
		}
	}
	return B_OK;
}


void StartFreezeWatchdog(const char *appName)
{
	bigtime_t threshold = 500000;
	if (const char *value = getenv("CODEEDIT_FREEZE_MS")) {
		threshold = static_cast<bigtime_t>(atoll(value)) * 1000;
		if (threshold <= 0) {
			return;
		}
	}
	Watchdog *watchdog = new Watchdog {appName, threshold};
	const thread_id thread = spawn_thread(Run, "freeze watchdog", B_NORMAL_PRIORITY, watchdog);
	if (thread >= 0) {
		resume_thread(thread);
	}
}


} // DiffKit
