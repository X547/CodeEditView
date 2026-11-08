#pragma once

#include <deque>
#include <functional>

#include <Locker.h>
#include <OS.h>


namespace Workspace {


// Runs jobs one after the other in a thread of its own.
class JobQueue {
private:
	BLocker fLock;
	std::deque<std::function<void()>> fJobs;
	sem_id fSem;
	thread_id fThread;
	bool fQuitting {};

	static status_t ThreadEntry(void *data);
	void Run();

public:
	explicit JobQueue(const char *name);
	// Waits for the running job; queued jobs are dropped.
	~JobQueue();

	JobQueue(const JobQueue &other) = delete;
	JobQueue &operator=(const JobQueue &other) = delete;

	void Post(std::function<void()> job);
	// Waits until the jobs posted so far are done; not from a job.
	void Wait();
};


} // Workspace
