#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <Message.h>
#include <Messenger.h>


namespace DiffKit {


// Runs slow work off a window's thread. Only the newest request counts: a
// request made while work runs replaces any waiting one, and the results of
// replaced work are dropped. When work finishes, the target gets a message
// with the given code, and Take() gives the result on the window's thread.
//
// The work runs on its own thread and must not use the window's objects:
// it captures copies (as a repository path, to open its own handle). The
// loader can be destroyed while work runs; that work finishes unseen, and
// the window does not wait for it.
template<typename Result>
class BackgroundLoader {
private:
	struct Shared {
		std::mutex lock;
		BMessenger target;
		uint32 what {};
		std::function<Result()> next;
		bool running {};
		bool closed {};
		std::optional<Result> result;
		std::string error;
		bool ready {};
	};

	std::shared_ptr<Shared> fShared;

	static void Run(std::shared_ptr<Shared> shared)
	{
		while (true) {
			std::function<Result()> work;
			{
				std::lock_guard<std::mutex> lock(shared->lock);
				if (shared->closed || !shared->next) {
					shared->running = false;
					return;
				}
				work = std::move(shared->next);
				shared->next = nullptr;
			}
			std::optional<Result> result;
			std::string error;
			try {
				result.emplace(work());
			} catch (const std::exception &e) {
				error = e.what();
			}
			BMessenger target;
			uint32 what = 0;
			{
				std::lock_guard<std::mutex> lock(shared->lock);
				// Newer work replaces this result.
				if (shared->closed || shared->next) {
					continue;
				}
				shared->result = std::move(result);
				shared->error = std::move(error);
				shared->ready = true;
				target = shared->target;
				what = shared->what;
			}
			BMessage message(what);
			target.SendMessage(&message);
		}
	}

public:
	BackgroundLoader(const BMessenger &target, uint32 what):
		fShared(std::make_shared<Shared>())
	{
		fShared->target = target;
		fShared->what = what;
	}

	~BackgroundLoader()
	{
		std::lock_guard<std::mutex> lock(fShared->lock);
		fShared->closed = true;
		fShared->next = nullptr;
	}

	BackgroundLoader(const BackgroundLoader &other) = delete;
	BackgroundLoader &operator=(const BackgroundLoader &other) = delete;

	// For a target that did not exist when the loader was created.
	void SetTarget(const BMessenger &target)
	{
		std::lock_guard<std::mutex> lock(fShared->lock);
		fShared->target = target;
	}

	void Load(std::function<Result()> work)
	{
		std::lock_guard<std::mutex> lock(fShared->lock);
		fShared->next = std::move(work);
		fShared->ready = false;
		fShared->result.reset();
		if (!fShared->running) {
			fShared->running = true;
			std::thread(Run, fShared).detach();
		}
	}

	// Whether work is running or waiting.
	bool IsLoading() const
	{
		std::lock_guard<std::mutex> lock(fShared->lock);
		return fShared->running || fShared->next;
	}

	// The result of the newest work, once: none if there is none yet, or if
	// the work failed (error says why).
	std::optional<Result> Take(std::string &error)
	{
		std::lock_guard<std::mutex> lock(fShared->lock);
		error.clear();
		if (!fShared->ready) {
			return std::nullopt;
		}
		fShared->ready = false;
		error = std::move(fShared->error);
		std::optional<Result> result = std::move(fShared->result);
		fShared->result.reset();
		return result;
	}
};


} // DiffKit
