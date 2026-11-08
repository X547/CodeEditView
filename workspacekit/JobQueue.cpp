#include "JobQueue.h"

#include <Autolock.h>


namespace Workspace {


JobQueue::JobQueue(const char *name):
	fLock("JobQueue"),
	fSem(create_sem(0, name)),
	fThread(spawn_thread(ThreadEntry, name, B_NORMAL_PRIORITY, this))
{
	resume_thread(fThread);
}


JobQueue::~JobQueue()
{
	{
		BAutolock lock(fLock);
		fQuitting = true;
		fJobs.clear();
	}
	release_sem(fSem);
	status_t result;
	wait_for_thread(fThread, &result);
	delete_sem(fSem);
}


status_t JobQueue::ThreadEntry(void *data)
{
	static_cast<JobQueue*>(data)->Run();
	return B_OK;
}


void JobQueue::Run()
{
	for (;;) {
		while (acquire_sem(fSem) == B_INTERRUPTED) {
		}
		std::function<void()> job;
		{
			BAutolock lock(fLock);
			if (fQuitting) {
				return;
			}
			if (fJobs.empty()) {
				continue;
			}
			job = std::move(fJobs.front());
			fJobs.pop_front();
		}
		try {
			job();
		} catch (...) {
			// Jobs report their errors themselves; the queue keeps running.
		}
	}
}


void JobQueue::Post(std::function<void()> job)
{
	{
		BAutolock lock(fLock);
		if (fQuitting) {
			return;
		}
		fJobs.push_back(std::move(job));
	}
	release_sem(fSem);
}


void JobQueue::Wait()
{
	const sem_id done = create_sem(0, "JobQueue done");
	Post([done] {
		release_sem(done);
	});
	while (acquire_sem(done) == B_INTERRUPTED) {
	}
	delete_sem(done);
}


} // Workspace
