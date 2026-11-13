#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include <Referenceable.h>


namespace CodeEdit {


// Persistent UTF-8 byte rope with a line index.
//
// Nodes are immutable once built and shared between copies, so copying a Rope
// is O(1) and yields an independent snapshot. Different Rope objects sharing
// nodes may be used from different threads concurrently; a single Rope object
// is not synchronized.
//
// All positions are byte offsets. Lines are separated by '\n'; a rope with N
// newlines has N + 1 lines.
class Rope {
private:
	class Node;

	BReference<Node> fRoot;

public:
	class Builder;

	Rope();
	explicit Rope(std::string_view text);
	Rope(const Rope &other);
	Rope &operator=(const Rope &other);
	~Rope();

	size_t Length() const;
	bool IsEmpty() const {return Length() == 0;}
	size_t NewlineCount() const;
	size_t LineCount() const {return NewlineCount() + 1;}

	void Insert(size_t offset, std::string_view text);
	void Erase(size_t offset, size_t length);
	void Replace(size_t offset, size_t length, std::string_view text);

	char ByteAt(size_t offset) const;
	std::string Substring(size_t offset, size_t length) const;
	std::string ToString() const;

	// Contiguous bytes from offset to the end of the containing chunk. Empty at
	// Length(). The view stays valid while any Rope sharing the chunk exists.
	std::string_view ChunkAt(size_t offset) const;

	// Offset of the first byte of a line.
	size_t LineStart(size_t line) const;
	// Offset of the line's terminating '\n', or Length() for the last line.
	size_t LineEnd(size_t line) const;
	// Number of newlines before offset.
	size_t LineOfOffset(size_t offset) const;

	// Debugging aids.
	uint32_t Height() const;
	bool Validate() const;
};


// Builds a rope from text given in pieces, without a contiguous copy of the
// whole text.
class Rope::Builder {
private:
	std::vector<BReference<Node>> fLeaves;
	std::string fPending;

public:
	Builder();
	~Builder();

	void Append(std::string_view text);
	// The rope of the text appended so far; the builder is empty afterwards.
	Rope Build();
};


} // CodeEdit
