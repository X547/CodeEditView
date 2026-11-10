#include "Ask.h"

#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

#include <Alert.h>
#include <Invoker.h>
#include <MessageFilter.h>
#include <Messenger.h>
#include <Window.h>


namespace DiffKit {


static const uint32 kMsgAnswer = 'DKan';


// Waiting callbacks and open dialogs, by owner. Owners are only compared,
// never used through these pointers.
struct Registry {
	std::mutex lock;
	uint64 nextId = 1;
	std::map<uint64, std::pair<const BLooper*, std::function<void(BMessage*)>>> answers;
	std::multimap<const BLooper*, BMessenger> dialogs;
	std::set<const BLooper*> filtered;
};


static Registry &TheRegistry()
{
	static Registry registry;
	return registry;
}


// Runs the callbacks of the answers sent to its window; deleted with the
// window, when it drops the window's callbacks and closes its dialogs.
class AnswerFilter: public BMessageFilter {
private:
	const BLooper *fOwner;

public:
	explicit AnswerFilter(const BLooper *owner):
		BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE, kMsgAnswer),
		fOwner(owner)
	{
	}

	~AnswerFilter() override
	{
		Registry &registry = TheRegistry();
		std::vector<BMessenger> dialogs;
		{
			std::lock_guard<std::mutex> lock(registry.lock);
			for (auto it = registry.answers.begin(); it != registry.answers.end();) {
				it = it->second.first == fOwner ? registry.answers.erase(it) : std::next(it);
			}
			const auto range = registry.dialogs.equal_range(fOwner);
			for (auto it = range.first; it != range.second; ++it) {
				dialogs.push_back(it->second);
			}
			registry.dialogs.erase(fOwner);
			registry.filtered.erase(fOwner);
		}
		for (const BMessenger &dialog : dialogs) {
			dialog.SendMessage(B_QUIT_REQUESTED);
		}
	}

	filter_result Filter(BMessage *message, BHandler **target) override
	{
		(void)target;
		Registry &registry = TheRegistry();
		std::function<void(BMessage*)> done;
		{
			std::lock_guard<std::mutex> lock(registry.lock);
			const auto found = registry.answers.find(message->GetUInt64("answer", 0));
			if (found != registry.answers.end()) {
				done = std::move(found->second.second);
				registry.answers.erase(found);
			}
		}
		if (done) {
			done(message);
		}
		return B_SKIP_MESSAGE;
	}
};


// The owner is locked: this runs on its thread.
static void WatchOwner(BWindow *owner)
{
	Registry &registry = TheRegistry();
	{
		std::lock_guard<std::mutex> lock(registry.lock);
		if (!registry.filtered.insert(owner).second) {
			return;
		}
	}
	owner->AddCommonFilter(new AnswerFilter(owner));
}


BMessage AnswerMessage(BWindow *owner, std::function<void(BMessage *answer)> done)
{
	WatchOwner(owner);
	Registry &registry = TheRegistry();
	uint64 id;
	{
		std::lock_guard<std::mutex> lock(registry.lock);
		id = registry.nextId++;
		registry.answers[id] = {owner, std::move(done)};
	}
	BMessage message(kMsgAnswer);
	message.AddUInt64("answer", id);
	return message;
}


void AttachDialog(BWindow *owner, BWindow *dialog)
{
	WatchOwner(owner);
	dialog->SetFeel(B_MODAL_SUBSET_WINDOW_FEEL);
	dialog->AddToSubset(owner);
	Registry &registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.lock);
	// Dialogs closed meanwhile are forgotten.
	const auto range = registry.dialogs.equal_range(owner);
	for (auto it = range.first; it != range.second;) {
		it = it->second.IsValid() ? std::next(it) : registry.dialogs.erase(it);
	}
	registry.dialogs.emplace(owner, BMessenger(dialog));
}


void Ask(BWindow *owner, BAlert *alert, std::function<void(int32 button)> done)
{
	AttachDialog(owner, alert);
	BMessage message = AnswerMessage(owner, [done = std::move(done)](BMessage *answer) {
		done(answer->GetInt32("which", -1));
	});
	alert->Go(new BInvoker(new BMessage(message), owner));
}


} // DiffKit
