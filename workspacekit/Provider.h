#pragma once

#include <stdint.h>

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "FileIO.h"
#include "Locator.h"


namespace Workspace {


class ProviderError: public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};


// The file changed since it was read: its version is not the expected one.
class VersionConflict: public ProviderError {
public:
	using ProviderError::ProviderError;
};


struct FileInfo {
	bool exists {};
	// Tells versions of the file apart (modification time and size, blob id,
	// ...); empty if the file does not exist.
	std::string version;
	// The file's MIME type (BEOS:TYPE attribute); empty if it has none or
	// the provider has no attributes.
	std::string mimeType;
};


struct FileData {
	// None: the file does not exist.
	std::optional<std::string> data;
	std::string version;
	std::string mimeType;
};


// Receives the data of a file as it is read.
class DataSink {
public:
	virtual ~DataSink() = default;

	// Drops the data passed so far: the file is read again from the start
	// (e.g. it was replaced while being read).
	virtual void Restart() = 0;
	virtual void Append(std::string_view data) = 0;
};


// An entry of a directory, with what the provider knows about it.
struct DirEntry {
	std::string name;
	// Can be listed (also a link to a directory).
	bool directory {};
	// A symbolic link.
	bool link {};
	// Of a file, in bytes.
	std::optional<uint64_t> size;
	// Seconds since the epoch.
	std::optional<int64_t> modified;
	// The MIME type the entry has (BEOS:TYPE attribute); empty if it has none
	// or the provider has no attributes.
	std::string mimeType;
};


// Reads and writes the files of one locator kind. Methods block (disk,
// repository, network) and must not be called with a model or looper lock
// held; they may be called from any thread. Errors throw ProviderError.
class Provider {
public:
	virtual ~Provider() = default;

	virtual bool IsWritable(const Locator &locator) const = 0;
	// The locator that identifies the file: e.g. a Git revision name becomes
	// a commit id and the repository its top directory.
	virtual Locator Resolve(const Locator &locator) = 0;
	// Passes the data to the sink as it is read; nothing if the file does not
	// exist.
	virtual FileInfo Read(const Locator &locator, DataSink &sink) = 0;
	// Replaces or creates the file with the data of the source, and returns
	// its new version. Unless overwrite is set, throws VersionConflict if the
	// file's version is not expectedVersion (empty: the file must not exist).
	virtual std::string Write(const Locator &locator, const DiffKit::ChunkSource &source,
		const std::string &expectedVersion, bool overwrite) = 0;

	// The same with the whole data at once.
	FileData Read(const Locator &locator);
	std::string Write(const Locator &locator, std::string_view data, const std::string &expectedVersion,
		bool overwrite);
	// Entries of a directory, sorted by name.
	virtual std::vector<DirEntry> List(const Locator &directory) = 0;
};


// The providers by locator kind. Thread safe after setup.
class Providers {
private:
	std::map<LocatorKind, std::unique_ptr<Provider>> fProviders;

public:
	// With the file and Git providers.
	static Providers &Default();

	void Add(LocatorKind kind, std::unique_ptr<Provider> provider);
	// nullptr if the kind is not supported.
	Provider *Find(LocatorKind kind) const;
	// Throws ProviderError if the kind is not supported.
	Provider &Get(const Locator &locator) const;
};


} // Workspace
