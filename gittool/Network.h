#pragma once

#include <string>
#include <vector>

#include "Snapshot.h"


namespace GitTool {


// The user side of a network operation. Called on the thread that runs the
// operation.
class NetworkHandler {
public:
	virtual ~NetworkHandler() = default;

	virtual bool IsCancelled() = 0;
	// Replaces the current progress line.
	virtual void Progress(const std::string &text) = 0;
	virtual void Log(const std::string &text) = 0;
	// Asks for a user name and password; false if the user cancelled.
	virtual bool AskPassword(const std::string &url, std::string &user, std::string &password) = 0;
	// Asks whether to trust a server whose identity cannot be verified.
	virtual bool ConfirmServer(const std::string &host, const std::string &details) = 0;
};


struct ServerRef {
	// Full name, as "refs/heads/main".
	std::string name;
	git_oid id {};
};


// Downloads from a remote; prune removes remote copies of branches deleted
// on the server, allTags downloads every tag.
void Download(git_repository *repository, const std::string &remote, bool prune, bool allTags, NetworkHandler &handler);
// Downloads refspecs ("+src:dst") instead of those of the remote's
// configuration; no tags.
void DownloadRefspecs(git_repository *repository, const std::string &remote, const std::vector<std::string> &refspecs,
	NetworkHandler &handler);
// Uploads refspecs ("src:dst", "+src:dst" to overwrite, ":dst" to delete).
// Throws if the server rejects any of them.
void Upload(git_repository *repository, const std::string &remote, const std::vector<std::string> &refspecs,
	NetworkHandler &handler);
// Branches and tags on the server, without downloading.
std::vector<ServerRef> ListServer(git_repository *repository, const std::string &remote, NetworkHandler &handler);


} // GitTool
