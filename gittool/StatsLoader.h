#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <Messenger.h>

#include "CommitDiff.h"


namespace GitTool {


// Computes commit statistics on a thread with its own repository handle,
// the most recently requested first, and sends kMsgStatsReady ("commit",
// raw git_oid) to the target for each. The results are kept for the whole
// application: a commit's statistics never change.
class StatsLoader {
private:
	std::string fRepositoryPath;
	BMessenger fTarget;
	std::mutex fLock;
	std::condition_variable fWake;
	std::vector<git_oid> fQueue;
	std::set<std::string> fQueued;
	bool fQuit {};
	std::thread fThread;

	void Run();

public:
	StatsLoader(const std::string &repositoryPath, const BMessenger &target);
	// Waits for the commit being computed.
	~StatsLoader();

	StatsLoader(const StatsLoader &other) = delete;
	StatsLoader &operator=(const StatsLoader &other) = delete;

	// Queues a commit unless its statistics are known or queued.
	void Request(const git_oid &commit);
	// Forgets the queued commits, e.g. when other rows are shown.
	void Clear();

	// None while not computed; zero counts if they could not be computed.
	static std::optional<CommitStats> Cached(const git_oid &commit);
};


} // GitTool
