#pragma once

#include <map>
#include <string>

#include <Locker.h>

#include "Git.h"
#include "Provider.h"


namespace Workspace {


// Files of Git commits, read-only. Data is the blob as stored, without
// filters. Repositories stay open for later reads.
class GitProvider: public Provider {
private:
	DiffKit::GitLibrary fLibrary;
	BLocker fLock;
	// By the directory given to Open().
	std::map<std::string, DiffKit::RepositoryPtr> fRepositories;

	git_repository *Open(const std::string &directory);
	DiffKit::TreePtr Tree(git_repository *repository, const Locator &locator);

public:
	GitProvider();

	bool IsWritable(const Locator &locator) const override;
	// The revision becomes the commit id it names, the repository its
	// working tree directory (the Git directory if bare).
	Locator Resolve(const Locator &locator) override;
	FileData Read(const Locator &locator) override;
	std::string Write(const Locator &locator, std::string_view data, const std::string &expectedVersion,
		bool overwrite) override;
	std::vector<DirEntry> List(const Locator &directory) override;
};


} // Workspace
