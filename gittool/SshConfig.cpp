#include "SshConfig.h"

#include <ctype.h>
#include <glob.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

#include <fstream>


namespace GitTool {


static constexpr int kMaxIncludeDepth = 16;


static std::string Lower(std::string text)
{
	for (char &c : text) {
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return text;
}


static std::string LocalUser()
{
	const passwd *entry = getpwuid(getuid());
	if (entry != nullptr && entry->pw_name != nullptr) {
		return entry->pw_name;
	}
	const char *name = getenv("USER");
	return name != nullptr ? name : "";
}


// A port number; an empty text is no port.
static bool ParsePort(const std::string &text, int &port)
{
	port = 0;
	if (text.empty()) {
		return true;
	}
	if (text.size() > 5) {
		return false;
	}
	int value = 0;
	for (const char c : text) {
		if (c < '0' || c > '9') {
			return false;
		}
		value = value * 10 + (c - '0');
	}
	if (value == 0 || value > 65535) {
		return false;
	}
	port = value;
	return true;
}


// "host", "host:port", "[host]" or "[host]:port".
static bool ParseHostPort(const std::string &text, std::string &host, int &port)
{
	port = 0;
	if (!text.empty() && text[0] == '[') {
		const size_t close = text.find(']');
		if (close == std::string::npos) {
			return false;
		}
		host = text.substr(1, close - 1);
		const std::string rest = text.substr(close + 1);
		if (!rest.empty() && (rest[0] != ':' || !ParsePort(rest.substr(1), port))) {
			return false;
		}
	} else {
		const size_t colon = text.find(':');
		host = text.substr(0, colon);
		if (colon != std::string::npos && !ParsePort(text.substr(colon + 1), port)) {
			return false;
		}
	}
	return !host.empty();
}


std::optional<SshUrl> SshUrl::Parse(const std::string &url)
{
	SshUrl result;
	const size_t scheme = url.find("://");
	if (scheme != std::string::npos) {
		const std::string name = url.substr(0, scheme);
		if (name != "ssh" && name != "ssh+git" && name != "git+ssh") {
			return std::nullopt;
		}
		const size_t start = scheme + 3;
		const size_t slash = url.find('/', start);
		std::string authority = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
		if (slash != std::string::npos) {
			result.path = url.substr(slash);
		}
		const size_t at = authority.rfind('@');
		if (at != std::string::npos) {
			result.user = authority.substr(0, at);
			authority.erase(0, at + 1);
		}
		if (!ParseHostPort(authority, result.host, result.port)) {
			return std::nullopt;
		}
		return result;
	}

	// "[user@]host:path", where the colon comes before any slash.
	const size_t firstColon = url.find(':');
	const size_t firstSlash = url.find('/');
	if (firstColon == std::string::npos || firstSlash < firstColon) {
		return std::nullopt;
	}
	size_t hostStart = 0;
	const size_t at = url.find('@');
	if (at < firstColon) {
		result.user = url.substr(0, at);
		hostStart = at + 1;
	}
	size_t colon;
	if (hostStart < url.size() && url[hostStart] == '[') {
		const size_t close = url.find(']', hostStart);
		if (close == std::string::npos || close + 1 >= url.size() || url[close + 1] != ':') {
			return std::nullopt;
		}
		result.host = url.substr(hostStart + 1, close - hostStart - 1);
		colon = close + 1;
	} else {
		colon = url.find(':', hostStart);
		if (colon == std::string::npos) {
			return std::nullopt;
		}
		result.host = url.substr(hostStart, colon - hostStart);
	}
	if (result.host.empty()) {
		return std::nullopt;
	}
	result.path = url.substr(colon + 1);
	result.scpLike = true;
	return result;
}


std::string SshUrl::ToString() const
{
	const std::string userPart = user.empty() ? "" : user + "@";
	const bool ipv6 = host.find(':') != std::string::npos;
	if (scpLike && port == 0 && !ipv6) {
		return userPart + host + ":" + path;
	}
	std::string text = "ssh://" + userPart + (ipv6 ? "[" + host + "]" : host);
	if (port != 0) {
		text += ":" + std::to_string(port);
	}
	if (scpLike && (path.empty() || path[0] != '/')) {
		// A relative scp-like path is in the home directory; libgit2 sends
		// "/~/path" as "~/path", which the server expands.
		text += path.compare(0, 1, "~") == 0 ? "/" + path : "/~/" + path;
	} else {
		text += path;
	}
	return text;
}


std::vector<std::string> SshConfigFiles(const std::string &home)
{
	std::vector<std::vector<std::string>> candidateGroups;
	if (!home.empty()) {
		candidateGroups.push_back({home + "/config/settings/ssh/config", home + "/.ssh/config"});
	}
	candidateGroups.push_back({"/boot/system/settings/ssh/ssh_config", "/etc/ssh/ssh_config"});
	std::vector<std::string> files;
	for (const std::vector<std::string> &candidates : candidateGroups) {
		for (const std::string &candidate : candidates) {
			if (access(candidate.c_str(), R_OK) == 0) {
				files.push_back(candidate);
				break;
			}
		}
	}
	return files;
}


//#pragma mark - Resolver

namespace {


// Splits a configuration line into the keyword and its arguments; empty for a
// blank line or a comment.
std::vector<std::string> SplitLine(const std::string &line)
{
	std::vector<std::string> words;
	size_t i = 0;
	for (;;) {
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
			i++;
		}
		// "Keyword=value" and "Keyword = value" are the same as "Keyword value".
		if (words.size() == 1 && i < line.size() && line[i] == '=') {
			i++;
			continue;
		}
		if (i >= line.size() || line[i] == '#') {
			break;
		}
		std::string word;
		bool quoted = false;
		while (i < line.size()) {
			const char c = line[i];
			if (!quoted && (c == ' ' || c == '\t' || (words.empty() && c == '='))) {
				break;
			}
			if (c == '"') {
				quoted = !quoted;
			} else {
				word += c;
			}
			i++;
		}
		words.push_back(std::move(word));
	}
	return words;
}


// An ssh pattern with "*" and "?", ignoring case as ssh does for host names.
bool Wildcard(const char *pattern, const char *text)
{
	for (; *pattern != '\0'; pattern++, text++) {
		if (*pattern == '*') {
			for (const char *rest = text; ; rest++) {
				if (Wildcard(pattern + 1, rest)) {
					return true;
				}
				if (*rest == '\0') {
					return false;
				}
			}
		}
		if (*text == '\0') {
			return false;
		}
		if (*pattern != '?' && tolower(static_cast<unsigned char>(*pattern)) != tolower(static_cast<unsigned char>(*text))) {
			return false;
		}
	}
	return *text == '\0';
}


// Patterns separated by spaces or commas; a "!" pattern that matches excludes
// the text whatever else matches.
bool MatchPatternList(const std::vector<std::string> &items, size_t first, size_t last, const std::string &text)
{
	bool matched = false;
	for (size_t index = first; index < last; index++) {
		const std::string &item = items[index];
		size_t start = 0;
		while (start <= item.size()) {
			size_t comma = item.find(',', start);
			if (comma == std::string::npos) {
				comma = item.size();
			}
			std::string pattern = item.substr(start, comma - start);
			start = comma + 1;
			if (pattern.empty()) {
				continue;
			}
			const bool negated = pattern[0] == '!';
			if (negated) {
				pattern.erase(0, 1);
			}
			if (Wildcard(pattern.c_str(), text.c_str())) {
				if (negated) {
					return false;
				}
				matched = true;
			}
		}
	}
	return matched;
}


class Resolver {
private:
	const std::string &fHome;
	const std::string &fAlias;
	const std::string fLocalUser = LocalUser();
	std::optional<std::string> fHostName;
	std::optional<std::string> fUser;
	std::optional<int> fPort;
	std::vector<std::string> fIdentityFiles;
	std::optional<bool> fIdentitiesOnly;
	std::optional<std::string> fProxy;

	bool Match(const std::vector<std::string> &words) const;
	void Include(const std::string &pattern, const std::string &baseDirectory, int depth);
	std::string ExpandTokens(const std::string &text, const SshHost &host) const;

public:
	Resolver(const std::string &home, const std::string &alias, const std::string &user, int port):
		fHome(home),
		fAlias(alias)
	{
		if (!user.empty()) {
			fUser = user;
		}
		if (port != 0) {
			fPort = port;
		}
	}

	void ReadFile(const std::string &path, const std::string &baseDirectory, bool active, int depth);
	SshHost Result() const;
};


bool Resolver::Match(const std::vector<std::string> &words) const
{
	for (size_t i = 1; i < words.size(); i++) {
		std::string criterion = Lower(words[i]);
		const bool negated = !criterion.empty() && criterion[0] == '!';
		if (negated) {
			criterion.erase(0, 1);
		}
		bool result = false;
		if (criterion == "all" || criterion == "final") {
			result = true;
		} else if (criterion == "canonical") {
			result = false;
		} else {
			if (i + 1 >= words.size()) {
				return false;
			}
			const std::string &argument = words[++i];
			const std::vector<std::string> patterns {argument};
			if (criterion == "host") {
				result = MatchPatternList(patterns, 0, 1, fHostName.value_or(fAlias));
			} else if (criterion == "originalhost") {
				result = MatchPatternList(patterns, 0, 1, fAlias);
			} else if (criterion == "user") {
				result = MatchPatternList(patterns, 0, 1, fUser.value_or(fLocalUser));
			} else if (criterion == "localuser") {
				result = MatchPatternList(patterns, 0, 1, fLocalUser);
			}
			// Exec and the other criteria cannot be evaluated here.
		}
		if (result == negated) {
			return false;
		}
	}
	return true;
}


void Resolver::Include(const std::string &pattern, const std::string &baseDirectory, int depth)
{
	std::string path = pattern;
	if (path == "~" || path.compare(0, 2, "~/") == 0) {
		path = fHome + path.substr(1);
	} else if (path.empty() || path[0] != '/') {
		path = baseDirectory + "/" + path;
	}
	glob_t matches {};
	if (glob(path.c_str(), 0, nullptr, &matches) == 0) {
		for (size_t i = 0; i < matches.gl_pathc; i++) {
			ReadFile(matches.gl_pathv[i], baseDirectory, true, depth + 1);
		}
	}
	globfree(&matches);
}


void Resolver::ReadFile(const std::string &path, const std::string &baseDirectory, bool active, int depth)
{
	if (depth > kMaxIncludeDepth) {
		return;
	}
	std::ifstream file(path);
	std::string line;
	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const std::vector<std::string> words = SplitLine(line);
		if (words.empty()) {
			continue;
		}
		const std::string keyword = Lower(words[0]);
		if (keyword == "host") {
			active = MatchPatternList(words, 1, words.size(), fAlias);
			continue;
		}
		if (keyword == "match") {
			active = Match(words);
			continue;
		}
		if (!active || words.size() < 2) {
			continue;
		}
		const std::string &value = words[1];
		if (keyword == "include") {
			for (size_t i = 1; i < words.size(); i++) {
				Include(words[i], baseDirectory, depth);
			}
		} else if (keyword == "hostname") {
			if (!fHostName.has_value()) {
				SshHost aliasOnly;
				aliasOnly.hostName = fAlias;
				fHostName = ExpandTokens(value, aliasOnly);
			}
		} else if (keyword == "user") {
			if (!fUser.has_value()) {
				fUser = value;
			}
		} else if (keyword == "port") {
			int port = 0;
			if (!fPort.has_value() && ParsePort(value, port) && port != 0) {
				fPort = port;
			}
		} else if (keyword == "identityfile") {
			if (Lower(value) != "none") {
				fIdentityFiles.push_back(value);
			}
		} else if (keyword == "identitiesonly") {
			if (!fIdentitiesOnly.has_value()) {
				fIdentitiesOnly = Lower(value) == "yes" || Lower(value) == "true";
			}
		} else if (keyword == "proxyjump" || keyword == "proxycommand") {
			if (!fProxy.has_value()) {
				std::string proxy;
				if (Lower(value) != "none") {
					proxy = words[0];
					for (size_t i = 1; i < words.size(); i++) {
						proxy += " " + words[i];
					}
				}
				fProxy = proxy;
			}
		}
	}
}


// "~" and the ssh "%" tokens of host-specific settings.
std::string Resolver::ExpandTokens(const std::string &text, const SshHost &host) const
{
	std::string result;
	size_t i = 0;
	if (text == "~" || text.compare(0, 2, "~/") == 0) {
		result = fHome;
		i = 1;
	}
	for (; i < text.size(); i++) {
		if (text[i] != '%' || i + 1 >= text.size()) {
			result += text[i];
			continue;
		}
		const char token = text[++i];
		switch (token) {
			case '%':
				result += '%';
				break;
			case 'd':
				result += fHome;
				break;
			case 'h':
				result += host.hostName;
				break;
			case 'n':
				result += fAlias;
				break;
			case 'p':
				result += std::to_string(host.port != 0 ? host.port : 22);
				break;
			case 'r':
				result += host.user;
				break;
			case 'u':
				result += fLocalUser;
				break;
			default:
				result += '%';
				result += token;
				break;
		}
	}
	return result;
}


SshHost Resolver::Result() const
{
	SshHost host;
	host.hostName = fHostName.value_or(fAlias);
	host.user = fUser.value_or("");
	host.port = fPort.value_or(0);
	host.identitiesOnly = fIdentitiesOnly.value_or(false);
	host.proxy = fProxy.value_or("");
	for (const std::string &file : fIdentityFiles) {
		host.identityFiles.push_back(ExpandTokens(file, host));
	}
	return host;
}


} // namespace


SshHost ResolveSshHost(
	const std::vector<std::string> &configFiles,
	const std::string &home,
	const std::string &alias,
	const std::string &user,
	int port
)
{
	Resolver resolver(home, alias, user, port);
	for (const std::string &path : configFiles) {
		const size_t slash = path.rfind('/');
		const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash);
		resolver.ReadFile(path, directory, true, 0);
	}
	return resolver.Result();
}


} // GitTool
