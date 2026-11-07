#include "StatsLoader.h"

#include <exception>
#include <map>

#include "Messages.h"


namespace GitTool {


static std::mutex sCacheLock;
static std::map<std::string, CommitStats> sCache;


std::optional<CommitStats> StatsLoader::Cached(const git_oid &commit)
{
	std::lock_guard<std::mutex> lock(sCacheLock);
	const auto found = sCache.find(OidString(commit));
	if (found == sCache.end()) {
		return std::nullopt;
	}
	return found->second;
}


StatsLoader::StatsLoader(const std::string &repositoryPath, const BMessenger &target):
	fRepositoryPath(repositoryPath),
	fTarget(target)
{
	fThread = std::thread([this]() {
		Run();
	});
}


StatsLoader::~StatsLoader()
{
	{
		std::lock_guard<std::mutex> lock(fLock);
		fQuit = true;
	}
	fWake.notify_one();
	fThread.join();
}


void StatsLoader::Request(const git_oid &commit)
{
	if (Cached(commit).has_value()) {
		return;
	}
	{
		std::lock_guard<std::mutex> lock(fLock);
		if (!fQueued.insert(OidString(commit)).second) {
			return;
		}
		fQueue.push_back(commit);
	}
	fWake.notify_one();
}


void StatsLoader::Clear()
{
	std::lock_guard<std::mutex> lock(fLock);
	fQueue.clear();
	fQueued.clear();
}


void StatsLoader::Run()
{
	RepositoryPtr repository;
	try {
		repository = OpenRepository(fRepositoryPath);
	} catch (const std::exception &e) {
		return;
	}
	while (true) {
		git_oid commit;
		{
			std::unique_lock<std::mutex> lock(fLock);
			fWake.wait(lock, [this]() {
				return fQuit || !fQueue.empty();
			});
			if (fQuit) {
				return;
			}
			// The newest request is for the rows drawn last.
			commit = fQueue.back();
			fQueue.pop_back();
			fQueued.erase(OidString(commit));
		}
		CommitStats stats;
		try {
			stats = ReadCommitStats(repository.get(), commit);
		} catch (const std::exception &e) {
		}
		{
			std::lock_guard<std::mutex> lock(sCacheLock);
			sCache[OidString(commit)] = stats;
		}
		BMessage ready(kMsgStatsReady);
		ready.AddData("commit", B_RAW_TYPE, &commit, sizeof(commit));
		fTarget.SendMessage(&ready);
	}
}


} // GitTool
