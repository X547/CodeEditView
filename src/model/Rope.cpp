#include <CodeEditView/Rope.h>

#include <string.h>

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>


namespace CodeEdit {


// A full leaf with its node takes one 2 KiB heap block.
static constexpr size_t kLeafBlockSize = 2048;
static constexpr size_t kMaxLeafBytes = 1984;
static constexpr size_t kMinLeafBytes = kMaxLeafBytes / 4;
static constexpr size_t kMaxChildren = 16;
static constexpr size_t kMinChildren = kMaxChildren / 4;


static bool IsUtf8Continuation(char byte)
{
	return (static_cast<uint8_t>(byte) & 0xc0) == 0x80;
}


static size_t CountNewlines(std::string_view text)
{
	return std::count(text.begin(), text.end(), '\n');
}


//#pragma mark - Rope::Node

// The text of a leaf follows the node in the same heap block.
class Rope::Node: public BReferenceable {
private:
	struct TextSpace {
		size_t bytes;
	};

	static void *operator new(size_t size, TextSpace space);
	static void operator delete(void *block, TextSpace space);

public:
	using Ref = BReference<Node>;
	using RefList = std::vector<Ref>;

	size_t bytes {};
	size_t newlines {};
	uint32_t height {};
	RefList children;

	static void operator delete(void *block);

	bool IsLeaf() const {return height == 0;}
	bool IsUnderfull() const;
	std::string_view Text() const;

	static Ref MakeLeaf(std::string_view text);
	static Ref MakeInternal(RefList children);
	static RefList MakeLeaves(std::string_view text);
	static RefList Group(RefList nodes);
	static Ref BuildTree(RefList nodes);

	static RefList MergePair(const Node &left, const Node &right);
	static void FixUnderfull(RefList &children);

	static RefList Insert(const Node &node, size_t offset, std::string_view text);
	static Ref Erase(const Node &node, size_t offset, size_t length);

	static const Node &LeafAt(const Node &root, size_t &offset);
	static bool Validate(const Node &node, bool isRoot);
};


void *Rope::Node::operator new(size_t size, TextSpace space)
{
	return ::operator new(size + space.bytes);
}


void Rope::Node::operator delete(void *block, TextSpace space)
{
	(void)space;
	::operator delete(block);
}


void Rope::Node::operator delete(void *block)
{
	::operator delete(block);
}


std::string_view Rope::Node::Text() const
{
	if (!IsLeaf()) {
		return {};
	}
	return std::string_view(reinterpret_cast<const char*>(this + 1), bytes);
}


bool Rope::Node::IsUnderfull() const
{
	if (IsLeaf()) {
		return bytes < kMinLeafBytes;
	}
	return children.size() < kMinChildren;
}


Rope::Node::Ref Rope::Node::MakeLeaf(std::string_view text)
{
	static_assert(sizeof(Node) + kMaxLeafBytes <= kLeafBlockSize);
	Ref node(new(TextSpace {text.size()}) Node(), true);
	node->bytes = text.size();
	node->newlines = CountNewlines(text);
	memcpy(reinterpret_cast<char*>(node.Get() + 1), text.data(), text.size());
	return node;
}


Rope::Node::Ref Rope::Node::MakeInternal(RefList children)
{
	Ref node(new(TextSpace {0}) Node(), true);
	node->height = children.front()->height + 1;
	for (const Ref &child : children) {
		node->bytes += child->bytes;
		node->newlines += child->newlines;
	}
	node->children = std::move(children);
	return node;
}


// Splits text into near-equal leaves of at most kMaxLeafBytes, keeping UTF-8
// sequences inside one leaf where possible.
Rope::Node::RefList Rope::Node::MakeLeaves(std::string_view text)
{
	RefList leaves;
	size_t pos = 0;
	while (pos < text.size()) {
		const size_t remaining = text.size() - pos;
		const size_t chunkCount = (remaining + kMaxLeafBytes - 1) / kMaxLeafBytes;
		size_t end = pos + (remaining + chunkCount - 1) / chunkCount;
		if (end < text.size()) {
			size_t aligned = end;
			while (aligned > pos && IsUtf8Continuation(text[aligned])) {
				aligned--;
			}
			if (aligned > pos) {
				end = aligned;
			}
		}
		leaves.push_back(MakeLeaf(text.substr(pos, end - pos)));
		pos = end;
	}
	return leaves;
}


// Wraps same-height nodes into the minimal number of evenly filled parents.
Rope::Node::RefList Rope::Node::Group(RefList nodes)
{
	const size_t groupCount = (nodes.size() + kMaxChildren - 1) / kMaxChildren;
	RefList parents;
	parents.reserve(groupCount);
	size_t pos = 0;
	for (size_t i = 0; i < groupCount; i++) {
		const size_t end = nodes.size() * (i + 1) / groupCount;
		parents.push_back(MakeInternal(RefList(nodes.begin() + pos, nodes.begin() + end)));
		pos = end;
	}
	return parents;
}


Rope::Node::Ref Rope::Node::BuildTree(RefList nodes)
{
	if (nodes.empty()) {
		return MakeLeaf({});
	}
	while (nodes.size() > 1) {
		nodes = Group(std::move(nodes));
	}
	return nodes.front();
}


// Merges two adjacent same-height nodes into one, or redistributes their
// content evenly into two when it does not fit into one.
Rope::Node::RefList Rope::Node::MergePair(const Node &left, const Node &right)
{
	if (left.IsLeaf()) {
		std::string combined;
		combined.reserve(left.bytes + right.bytes);
		combined += left.Text();
		combined += right.Text();
		if (combined.size() <= kMaxLeafBytes) {
			return {MakeLeaf(combined)};
		}
		return MakeLeaves(combined);
	}

	RefList combined = left.children;
	combined.insert(combined.end(), right.children.begin(), right.children.end());
	if (combined.size() <= kMaxChildren) {
		return {MakeInternal(std::move(combined))};
	}
	return Group(std::move(combined));
}


void Rope::Node::FixUnderfull(RefList &children)
{
	size_t i = 0;
	while (i + 1 < children.size()) {
		if (!children[i]->IsUnderfull() && !children[i + 1]->IsUnderfull()) {
			i++;
			continue;
		}
		RefList merged = MergePair(*children[i], *children[i + 1]);
		const bool isSingle = merged.size() == 1;
		children.erase(children.begin() + i, children.begin() + i + 2);
		children.insert(children.begin() + i, merged.begin(), merged.end());
		if (!isSingle) {
			i++;
		}
	}
}


// Returns one or more nodes of the same height as node.
Rope::Node::RefList Rope::Node::Insert(const Node &node, size_t offset, std::string_view text)
{
	if (node.IsLeaf()) {
		const std::string_view oldText = node.Text();
		std::string combined;
		combined.reserve(oldText.size() + text.size());
		combined += oldText.substr(0, offset);
		combined += text;
		combined += oldText.substr(offset);
		if (combined.size() <= kMaxLeafBytes) {
			return {MakeLeaf(combined)};
		}
		return MakeLeaves(combined);
	}

	// At a child boundary prefer appending to the left child, so that typing
	// extends an existing leaf.
	size_t index = 0;
	size_t start = 0;
	while (index + 1 < node.children.size() && offset > start + node.children[index]->bytes) {
		start += node.children[index]->bytes;
		index++;
	}
	RefList parts = Insert(*node.children[index], offset - start, text);

	RefList children;
	children.reserve(node.children.size() + parts.size() - 1);
	children.insert(children.end(), node.children.begin(), node.children.begin() + index);
	children.insert(children.end(), parts.begin(), parts.end());
	children.insert(children.end(), node.children.begin() + index + 1, node.children.end());
	if (children.size() <= kMaxChildren) {
		return {MakeInternal(std::move(children))};
	}
	return Group(std::move(children));
}


// Returns a node of the same height as node, or an unset reference when
// nothing remains.
Rope::Node::Ref Rope::Node::Erase(const Node &node, size_t offset, size_t length)
{
	if (node.IsLeaf()) {
		std::string remaining;
		remaining.reserve(node.bytes - length);
		remaining.append(node.Text().substr(0, offset));
		remaining.append(node.Text().substr(offset + length));
		if (remaining.empty()) {
			return {};
		}
		return MakeLeaf(remaining);
	}

	const size_t eraseEnd = offset + length;
	RefList children;
	size_t start = 0;
	for (const Ref &child : node.children) {
		const size_t end = start + child->bytes;
		if (end <= offset || start >= eraseEnd) {
			children.push_back(child);
		} else if (start < offset || end > eraseEnd) {
			const size_t from = std::max(offset, start);
			const size_t to = std::min(end, eraseEnd);
			Ref rest = Erase(*child, from - start, to - from);
			if (rest.IsSet()) {
				children.push_back(rest);
			}
		}
		start = end;
	}
	if (children.empty()) {
		return {};
	}
	FixUnderfull(children);
	return MakeInternal(std::move(children));
}


// Finds the leaf containing offset (which must be < root.bytes) and converts
// offset to be relative to that leaf.
const Rope::Node &Rope::Node::LeafAt(const Node &root, size_t &offset)
{
	const Node *node = &root;
	while (!node->IsLeaf()) {
		size_t index = 0;
		while (offset >= node->children[index]->bytes) {
			offset -= node->children[index]->bytes;
			index++;
		}
		node = node->children[index].Get();
	}
	return *node;
}


bool Rope::Node::Validate(const Node &node, bool isRoot)
{
	if (node.IsLeaf()) {
		return node.children.empty()
			&& node.newlines == CountNewlines(node.Text())
			&& node.bytes <= kMaxLeafBytes
			&& (isRoot || node.bytes > 0);
	}

	if (node.children.empty() || node.children.size() > kMaxChildren) {
		return false;
	}
	if (isRoot && node.children.size() < 2) {
		return false;
	}
	size_t bytes = 0;
	size_t newlines = 0;
	for (const Ref &child : node.children) {
		if (!child.IsSet() || child->height + 1 != node.height || !Validate(*child, false)) {
			return false;
		}
		bytes += child->bytes;
		newlines += child->newlines;
	}
	return bytes == node.bytes && newlines == node.newlines;
}


//#pragma mark - Rope

Rope::Rope():
	fRoot(Node::MakeLeaf({}))
{
}


Rope::Rope(std::string_view text):
	fRoot(Node::BuildTree(Node::MakeLeaves(text)))
{
}


Rope::Rope(const Rope &other) = default;
Rope &Rope::operator=(const Rope &other) = default;
Rope::~Rope() = default;


size_t Rope::Length() const
{
	return fRoot->bytes;
}


size_t Rope::NewlineCount() const
{
	return fRoot->newlines;
}


void Rope::Insert(size_t offset, std::string_view text)
{
	if (offset > Length()) {
		throw std::out_of_range("Rope::Insert: offset out of range");
	}
	if (text.empty()) {
		return;
	}
	fRoot = Node::BuildTree(Node::Insert(*fRoot, offset, text));
}


void Rope::Erase(size_t offset, size_t length)
{
	if (offset > Length() || length > Length() - offset) {
		throw std::out_of_range("Rope::Erase: range out of range");
	}
	if (length == 0) {
		return;
	}
	Node::Ref root = Node::Erase(*fRoot, offset, length);
	if (!root.IsSet()) {
		fRoot = Node::MakeLeaf({});
		return;
	}
	while (!root->IsLeaf() && root->children.size() == 1) {
		const Node::Ref child = root->children.front();
		root = child;
	}
	fRoot = root;
}


void Rope::Replace(size_t offset, size_t length, std::string_view text)
{
	if (offset > Length() || length > Length() - offset) {
		throw std::out_of_range("Rope::Replace: range out of range");
	}
	Erase(offset, length);
	Insert(offset, text);
}


char Rope::ByteAt(size_t offset) const
{
	if (offset >= Length()) {
		throw std::out_of_range("Rope::ByteAt: offset out of range");
	}
	size_t local = offset;
	const Node &leaf = Node::LeafAt(*fRoot, local);
	return leaf.Text()[local];
}


std::string Rope::Substring(size_t offset, size_t length) const
{
	if (offset > Length() || length > Length() - offset) {
		throw std::out_of_range("Rope::Substring: range out of range");
	}
	std::string result;
	result.reserve(length);
	const size_t end = offset + length;
	size_t pos = offset;
	while (pos < end) {
		const std::string_view chunk = ChunkAt(pos).substr(0, end - pos);
		result += chunk;
		pos += chunk.size();
	}
	return result;
}


std::string Rope::ToString() const
{
	return Substring(0, Length());
}


std::string_view Rope::ChunkAt(size_t offset) const
{
	if (offset > Length()) {
		throw std::out_of_range("Rope::ChunkAt: offset out of range");
	}
	if (offset == Length()) {
		return {};
	}
	size_t local = offset;
	const Node &leaf = Node::LeafAt(*fRoot, local);
	return leaf.Text().substr(local);
}


size_t Rope::LineStart(size_t line) const
{
	if (line > NewlineCount()) {
		throw std::out_of_range("Rope::LineStart: line out of range");
	}
	if (line == 0) {
		return 0;
	}

	size_t remaining = line;
	size_t offset = 0;
	const Node *node = fRoot.Get();
	while (!node->IsLeaf()) {
		size_t index = 0;
		while (node->children[index]->newlines < remaining) {
			remaining -= node->children[index]->newlines;
			offset += node->children[index]->bytes;
			index++;
		}
		node = node->children[index].Get();
	}

	size_t pos = 0;
	for (;;) {
		pos = node->Text().find('\n', pos);
		remaining--;
		if (remaining == 0) {
			break;
		}
		pos++;
	}
	return offset + pos + 1;
}


size_t Rope::LineEnd(size_t line) const
{
	if (line == NewlineCount()) {
		return Length();
	}
	return LineStart(line + 1) - 1;
}


size_t Rope::LineOfOffset(size_t offset) const
{
	if (offset > Length()) {
		throw std::out_of_range("Rope::LineOfOffset: offset out of range");
	}

	size_t line = 0;
	size_t local = offset;
	const Node *node = fRoot.Get();
	while (!node->IsLeaf()) {
		const Node *next = nullptr;
		for (const Node::Ref &child : node->children) {
			if (local < child->bytes) {
				next = child.Get();
				break;
			}
			local -= child->bytes;
			line += child->newlines;
		}
		if (next == nullptr) {
			return line;
		}
		node = next;
	}
	const std::string_view text = node->Text();
	return line + std::count(text.begin(), text.begin() + local, '\n');
}


uint32_t Rope::Height() const
{
	return fRoot->height;
}


bool Rope::Validate() const
{
	return Node::Validate(*fRoot, true);
}


//#pragma mark - Rope::Builder

Rope::Builder::Builder() = default;
Rope::Builder::~Builder() = default;


// Full leaves are made as soon as possible; the last two leaves' worth stays
// pending, so Build() can split it evenly.
void Rope::Builder::Append(std::string_view text)
{
	fPending += text;
	if (fPending.size() < 2 * kMaxLeafBytes) {
		return;
	}
	size_t pos = 0;
	while (fPending.size() - pos >= 2 * kMaxLeafBytes) {
		size_t end = pos + kMaxLeafBytes;
		while (end > pos + 1 && IsUtf8Continuation(fPending[end])) {
			end--;
		}
		fLeaves.push_back(Node::MakeLeaf(fPending.substr(pos, end - pos)));
		pos = end;
	}
	fPending.erase(0, pos);
}


Rope Rope::Builder::Build()
{
	Node::RefList leaves = std::move(fLeaves);
	const Node::RefList last = Node::MakeLeaves(fPending);
	leaves.insert(leaves.end(), last.begin(), last.end());
	fLeaves.clear();
	fPending.clear();
	fPending.shrink_to_fit();

	Rope rope;
	rope.fRoot = Node::BuildTree(std::move(leaves));
	return rope;
}


} // CodeEdit
