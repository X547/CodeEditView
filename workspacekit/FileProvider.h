#pragma once

#include "Provider.h"


namespace Workspace {


// Local files. Writes go through a temporary file and a rename, keeping the
// permissions and attributes of the file.
class FileProvider: public Provider {
public:
	bool IsWritable(const Locator &locator) const override;
	Locator Resolve(const Locator &locator) override;
	using Provider::Read;
	using Provider::Write;
	FileInfo Read(const Locator &locator, DataSink &sink) override;
	std::string Write(const Locator &locator, const DiffKit::ChunkSource &source,
		const std::string &expectedVersion, bool overwrite) override;
	std::vector<DirEntry> List(const Locator &directory) override;

	// The version of a local file; empty if it does not exist.
	static std::string Version(const std::string &path);
};


} // Workspace
