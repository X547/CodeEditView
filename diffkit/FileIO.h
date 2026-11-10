#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>


namespace DiffKit {


class FileError: public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};


// "what path: strerror(errno)".
std::string ErrnoMessage(const std::string &what, const std::string &path);

// The contents of a regular file; none if it does not exist. Throws
// FileError.
std::optional<std::string> ReadFile(const std::string &path);

// Replaces or creates a file atomically: the data goes to a temporary file
// "<name>.save-XXXXXX" beside it, is synced and renamed over the file, so a
// failure or crash leaves the old file. An existing file's permissions,
// owner (where allowed) and attributes are kept. A symbolic link is kept and
// its target written. Throws FileError.
void WriteFile(const std::string &path, std::string_view data);

// Copies all attributes of one open node to another; returns false if one
// could not be copied.
bool CopyAttributes(int fromFd, int toFd);


} // DiffKit
