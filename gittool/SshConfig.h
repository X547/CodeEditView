#pragma once

#include <optional>
#include <string>
#include <vector>


namespace GitTool {


// The parts of an SSH remote URL: "ssh://user@host:port/path" or the scp-like
// "user@host:path".
struct SshUrl {
	std::string user;
	std::string host;
	// 0 when the URL has none.
	int port {};
	std::string path;
	bool scpLike {};

	// Empty for a URL of another protocol.
	static std::optional<SshUrl> Parse(const std::string &url);
	std::string ToString() const;
};


// What the OpenSSH client configuration says about a host, with the user and
// port of the URL taking precedence.
struct SshHost {
	std::string hostName;
	// Empty when neither the URL nor the configuration gives one.
	std::string user;
	// 0 when neither the URL nor the configuration gives one.
	int port {};
	// With "~" and "%" tokens expanded.
	std::vector<std::string> identityFiles;
	bool identitiesOnly {};
	// ProxyJump or ProxyCommand, which libgit2 cannot follow.
	std::string proxy;
};


// The user's and the system's OpenSSH client configuration files that exist,
// in the order ssh reads them. Haiku keeps them in "~/config/settings/ssh"
// and "/boot/system/settings/ssh".
std::vector<std::string> SshConfigFiles(const std::string &home);

// Reads configFiles for the host alias as ssh does: the first value of each
// setting wins. A relative Include is resolved against the directory of the
// configuration file it comes from.
SshHost ResolveSshHost(
	const std::vector<std::string> &configFiles,
	const std::string &home,
	const std::string &alias,
	const std::string &user,
	int port
);


} // GitTool
