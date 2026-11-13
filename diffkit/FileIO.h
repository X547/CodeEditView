#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>


namespace DiffKit {


// Receives data in pieces, in order.
using ChunkSink = std::function<void(std::string_view chunk)>;
// Produces data by passing it to the sink in pieces, in order.
using ChunkSource = std::function<void(const ChunkSink &sink)>;


class FileError: public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};


// "what path: strerror(errno)".
std::string ErrnoMessage(const std::string &what, const std::string &path);

// The contents of a regular file; none if it does not exist. Throws
// FileError.
std::optional<std::string> ReadFile(const std::string &path);
// Passes the contents of a regular file to the sink as it is read; false if
// it does not exist. Throws FileError.
bool ReadFile(const std::string &path, const ChunkSink &sink);

// Replaces or creates a file atomically: the data goes to a temporary file
// "<name>.save-XXXXXX" beside it, is synced and renamed over the file, so a
// failure or crash leaves the old file. An existing file's permissions,
// owner (where allowed) and attributes are kept. A symbolic link is kept and
// its target written. Throws FileError.
void WriteFile(const std::string &path, std::string_view data);
// As above, with the data written as the source produces it.
void WriteFile(const std::string &path, const ChunkSource &source);

// Copies all attributes of one open node to another; returns false if one
// could not be copied.
bool CopyAttributes(int fromFd, int toFd);


} // DiffKit
