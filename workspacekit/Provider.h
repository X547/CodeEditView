#pragma once

#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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


struct FileData {
	// None: the file does not exist.
	std::optional<std::string> data;
	// Tells versions of the file apart (modification time and size, blob id,
	// ...); empty if the file does not exist.
	std::string version;
};


struct DirEntry {
	std::string name;
	bool directory {};
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
	virtual FileData Read(const Locator &locator) = 0;
	// Replaces or creates the file and returns its new version. Unless
	// overwrite is set, throws VersionConflict if the file's version is not
	// expectedVersion (empty: the file must not exist).
	virtual std::string Write(const Locator &locator, std::string_view data, const std::string &expectedVersion,
		bool overwrite) = 0;
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
