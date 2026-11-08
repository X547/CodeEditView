#include "DocumentWatcher.h"

#include <sys/stat.h>

#include <MessageRunner.h>
#include <NodeMonitor.h>

#include "Document.h"


namespace Workspace {


static constexpr uint32 kMsgUpdate = 'DWup';
static constexpr uint32 kMsgFlush = 'DWfl';
static constexpr bigtime_t kFlushDelay = 200000;


static std::optional<node_ref> NodeOf(const std::string &path)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0) {
		return std::nullopt;
	}
	node_ref node;
	node.device = info.st_dev;
	node.node = info.st_ino;
	return node;
}


DocumentWatcher::DocumentWatcher(DocumentRegistry &registry):
	BLooper("DocumentWatcher"),
	fRegistry(registry)
{
}


DocumentWatcher::~DocumentWatcher()
{
	delete fFlushRunner;
	stop_watching(BMessenger(this));
}


void DocumentWatcher::Update()
{
	PostMessage(kMsgUpdate);
}


void DocumentWatcher::WatchNode(const node_ref &node, uint32 flags)
{
	if (fNodeWatches[node]++ == 0) {
		watch_node(&node, flags, BMessenger(this));
	}
}


void DocumentWatcher::UnwatchNode(const node_ref &node)
{
	const auto it = fNodeWatches.find(node);
	if (it == fNodeWatches.end()) {
		return;
	}
	if (--it->second == 0) {
		watch_node(&node, B_STOP_WATCHING, BMessenger(this));
		fNodeWatches.erase(it);
	}
}


void DocumentWatcher::Unwatch(const Watched &watched)
{
	UnwatchNode(watched.directory);
	if (watched.file.has_value()) {
		UnwatchNode(*watched.file);
	}
}


void DocumentWatcher::Sync()
{
	std::set<Locator> current;
	for (const BReference<Document> &document : fRegistry.All()) {
		if (document->GetLocator().Kind() == LocatorKind::File) {
			current.insert(document->GetLocator());
		}
	}

	for (auto it = fWatched.begin(); it != fWatched.end();) {
		if (current.contains(it->first)) {
			++it;
			continue;
		}
		Unwatch(it->second);
		fPending.erase(it->first);
		it = fWatched.erase(it);
	}

	for (const Locator &locator : current) {
		if (fWatched.contains(locator)) {
			continue;
		}
		const std::optional<node_ref> directory = NodeOf(locator.Parent().Path());
		if (!directory.has_value()) {
			continue;
		}
		Watched watched;
		watched.directory = *directory;
		WatchNode(watched.directory, B_WATCH_DIRECTORY);
		watched.file = NodeOf(locator.Path());
		if (watched.file.has_value()) {
			WatchNode(*watched.file, B_WATCH_STAT);
		}
		fWatched.emplace(locator, watched);
	}
}


void DocumentWatcher::HandleNodeMonitor(const BMessage *message)
{
	const int32 opcode = message->GetInt32("opcode", 0);
	if (opcode == B_STAT_CHANGED) {
		node_ref node;
		node.device = message->GetInt32("device", 0);
		node.node = message->GetInt64("node", 0);
		for (const auto &[locator, watched] : fWatched) {
			if (watched.file == node) {
				Changed(locator);
			}
		}
		return;
	}

	// An entry of a watched directory changed: the file may have been
	// created, deleted or replaced.
	std::set<node_ref> directories;
	const dev_t device = message->GetInt32("device", 0);
	for (const char *field : {"directory", "from directory", "to directory"}) {
		ino_t directory;
		if (message->FindInt64(field, &directory) == B_OK) {
			node_ref node;
			node.device = device;
			node.node = directory;
			directories.insert(node);
		}
	}
	for (auto &[locator, watched] : fWatched) {
		if (!directories.contains(watched.directory)) {
			continue;
		}
		const std::optional<node_ref> file = NodeOf(locator.Path());
		if (file == watched.file) {
			continue;
		}
		if (watched.file.has_value()) {
			UnwatchNode(*watched.file);
		}
		watched.file = file;
		if (file.has_value()) {
			WatchNode(*file, B_WATCH_STAT);
		}
		Changed(locator);
	}
}


void DocumentWatcher::Changed(const Locator &locator)
{
	fPending.insert(locator);
	if (fFlushRunner == nullptr) {
		BMessage flush(kMsgFlush);
		fFlushRunner = new BMessageRunner(BMessenger(this), &flush, kFlushDelay, 1);
	}
}


void DocumentWatcher::Flush()
{
	delete fFlushRunner;
	fFlushRunner = nullptr;
	for (const Locator &locator : fPending) {
		if (BReference<Document> document = fRegistry.Find(locator); document.Get() != nullptr) {
			fRegistry.Reload(document);
		}
	}
	fPending.clear();
}


void DocumentWatcher::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgUpdate:
			Sync();
			break;
		case kMsgFlush:
			Flush();
			break;
		case B_NODE_MONITOR:
			HandleNodeMonitor(message);
			break;
		default:
			BLooper::MessageReceived(message);
			break;
	}
}


} // Workspace
