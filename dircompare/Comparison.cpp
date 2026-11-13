#include "Comparison.h"

#include <algorithm>


namespace DirCompare {


static std::string ParentPath(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}


static std::string LeafName(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}


// Directories first, then by name.
static bool Before(const Node &a, const Node &b)
{
	const bool aDirectory = a.IsDirectory();
	const bool bDirectory = b.IsDirectory();
	if (aDirectory != bDirectory) {
		return aDirectory;
	}
	return a.name < b.name;
}


Comparison::Comparison()
{
	fRoot.sides[0].type = EntryType::Directory;
	fRoot.sides[1].type = EntryType::Directory;
	fNodes[""] = &fRoot;
}


Node *Comparison::Find(const std::string &path)
{
	const auto it = fNodes.find(path);
	return it != fNodes.end() ? it->second : nullptr;
}


Node *Comparison::Add(const ScanEntry &entry)
{
	Node *parent = Find(ParentPath(entry.path));
	if (parent == nullptr || Find(entry.path) != nullptr) {
		return nullptr;
	}
	std::unique_ptr<Node> node = std::make_unique<Node>();
	node->path = entry.path;
	node->name = LeafName(entry.path);
	node->sides[0] = entry.sides[0];
	node->sides[1] = entry.sides[1];
	node->own = entry.status;
	node->shown = entry.status;
	node->parent = parent;
	Node *added = node.get();
	const auto position = std::upper_bound(parent->children.begin(), parent->children.end(), node,
		[](const std::unique_ptr<Node> &a, const std::unique_ptr<Node> &b) {return Before(*a, *b);});
	parent->children.insert(position, std::move(node));
	parent->listed = true;
	fNodes[added->path] = added;
	return added;
}


std::vector<Node*> Comparison::AddEntries(const std::vector<ScanEntry> &entries)
{
	std::vector<Node*> added;
	for (const ScanEntry &entry : entries) {
		if (Node *node = Add(entry)) {
			added.push_back(node);
		}
	}
	return added;
}


void Comparison::ApplyChanges(const std::vector<StatusChange> &changes)
{
	for (const StatusChange &change : changes) {
		if (Node *node = Find(change.first)) {
			node->own = change.second;
		}
	}
}


void Comparison::SetComplete()
{
	fComplete = true;
}


void Comparison::Forget(Node &node)
{
	for (const std::unique_ptr<Node> &child : node.children) {
		Forget(*child);
	}
	fNodes.erase(node.path);
}


void Comparison::ReplaceSubtree(const std::string &path, const std::optional<ScanEntry> &self,
	const std::vector<ScanEntry> &contents)
{
	Node *node = nullptr;
	if (path.empty()) {
		for (const std::unique_ptr<Node> &child : fRoot.children) {
			Forget(*child);
		}
		fRoot.children.clear();
		node = &fRoot;
	} else {
		// Removed and added again: its type, and so its place, may change.
		if (Node *old = Find(path)) {
			Forget(*old);
			auto &siblings = old->parent->children;
			siblings.erase(std::find_if(siblings.begin(), siblings.end(),
				[old](const std::unique_ptr<Node> &child) {return child.get() == old;}));
		}
		if (!self.has_value()) {
			return;
		}
		node = Add(*self);
		if (node == nullptr) {
			return;
		}
	}
	AddEntries(contents);
	// The rescan reported all contents, also of empty directories.
	std::vector<Node*> stack {node};
	while (!stack.empty()) {
		Node *current = stack.back();
		stack.pop_back();
		current->listed = true;
		for (const std::unique_ptr<Node> &child : current->children) {
			stack.push_back(child.get());
		}
	}
}


// Post-order; returns whether the node's shown status changed.
bool Comparison::UpdateNode(Node &node, std::vector<Node*> &changed)
{
	std::vector<EntryStatus> children;
	children.reserve(node.children.size());
	for (const std::unique_ptr<Node> &child : node.children) {
		UpdateNode(*child, changed);
		children.push_back(child->shown);
	}
	EntryStatus shown = AggregateStatus(node.own, children);
	if (shown == EntryStatus::Same && node.IsDirectory() && !node.listed && !fComplete) {
		shown = EntryStatus::Pending;
	}
	if (shown == node.shown) {
		return false;
	}
	node.shown = shown;
	changed.push_back(&node);
	return true;
}


std::vector<Node*> Comparison::UpdateStatuses()
{
	std::vector<Node*> changed;
	UpdateNode(fRoot, changed);
	return changed;
}


//#pragma mark - BuildComparison

namespace {

class BuildingSink: public ScanSink {
private:
	Comparison &fComparison;
	BuildProgress fProgress;
	const std::function<void(const BuildProgress &progress)> &fReport;

public:
	BuildingSink(Comparison &comparison, const std::function<void(const BuildProgress &progress)> &report):
		fComparison(comparison),
		fReport(report)
	{
	}

	void EntriesFound(std::vector<ScanEntry> entries) override
	{
		for (const ScanEntry &entry : entries) {
			if (entry.status == EntryStatus::Pending) {
				fProgress.filesToCompare++;
				fProgress.bytesToCompare += entry.sides[0].size;
			}
		}
		fProgress.entries += entries.size();
		fComparison.AddEntries(entries);
		fReport(fProgress);
	}

	void StatusesChanged(std::vector<StatusChange> changes) override
	{
		for (const StatusChange &change : changes) {
			const Node *node = fComparison.Find(change.first);
			if (node != nullptr && !node->IsDirectory() && node->own == EntryStatus::Pending) {
				fProgress.comparingContents = true;
				fProgress.filesCompared++;
				fProgress.bytesCompared += node->sides[0].size;
			}
		}
		fComparison.ApplyChanges(changes);
		fReport(fProgress);
	}
};

}


std::unique_ptr<Comparison> BuildComparison(
	const std::string &leftRoot,
	const std::string &rightRoot,
	const ScanOptions &options,
	const std::atomic<bool> &cancel,
	const std::function<void(const BuildProgress &progress)> &report
)
{
	std::unique_ptr<Comparison> comparison = std::make_unique<Comparison>();
	BuildingSink sink(*comparison, report);
	if (!Scan(leftRoot, rightRoot, options, sink, cancel)) {
		return nullptr;
	}
	comparison->SetComplete();
	comparison->UpdateStatuses();
	return comparison;
}


} // DirCompare
