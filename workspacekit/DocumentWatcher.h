#pragma once

#include <map>
#include <optional>
#include <set>

#include <Looper.h>
#include <Node.h>

#include "Locator.h"


class BMessageRunner;


namespace Workspace {


class DocumentRegistry;


// Reloads the documents of local files when the files change elsewhere
// (DocumentRegistry::Reload(): unsaved edits are kept). Watches each file
// and its directory, as saving elsewhere often replaces the file by a
// rename. Changes are collected for a moment before reloading.
class DocumentWatcher: public BLooper {
private:
	struct Watched {
		node_ref directory;
		// None while the file does not exist.
		std::optional<node_ref> file;
	};

	DocumentRegistry &fRegistry;
	std::map<Locator, Watched> fWatched;
	// Watch counts, as several documents may share a node.
	std::map<node_ref, int32> fNodeWatches;
	std::set<Locator> fPending;
	BMessageRunner *fFlushRunner {};

	void Sync();
	void WatchNode(const node_ref &node, uint32 flags);
	void UnwatchNode(const node_ref &node);
	void Unwatch(const Watched &watched);
	void HandleNodeMonitor(const BMessage *message);
	void Changed(const Locator &locator);
	void Flush();

public:
	explicit DocumentWatcher(DocumentRegistry &registry);
	~DocumentWatcher() override;

	// Starts and stops watching to match the documents of the registry;
	// call after opening or closing documents. Thread safe.
	void Update();

	void MessageReceived(BMessage *message) override;
};


} // Workspace
