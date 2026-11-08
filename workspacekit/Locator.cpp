#include "Locator.h"

#include <limits.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <vector>


namespace Workspace {


static constexpr std::string_view kDash = " \xe2\x80\x94 ";
static constexpr size_t kOidLength = 40;
static constexpr size_t kShortOidLength = 7;


static bool IsHex(char ch)
{
	return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}


static bool IsFullOid(std::string_view text)
{
	return text.size() == kOidLength && std::all_of(text.begin(), text.end(), IsHex);
}


static std::string JoinPath(std::string_view directory, std::string_view name)
{
	if (directory.empty()) {
		return std::string(name);
	}
	std::string result(directory);
	if (result.back() != '/') {
		result += '/';
	}
	result += name;
	return result;
}


std::string NormalizePath(std::string_view path)
{
	const bool absolute = path.starts_with('/');
	std::vector<std::string_view> components;
	size_t start = 0;
	while (start <= path.size()) {
		size_t end = path.find('/', start);
		if (end == std::string_view::npos) {
			end = path.size();
		}
		const std::string_view component = path.substr(start, end - start);
		if (component == "..") {
			if (!components.empty() && components.back() != "..") {
				components.pop_back();
			} else if (!absolute) {
				components.push_back(component);
			}
		} else if (!component.empty() && component != ".") {
			components.push_back(component);
		}
		start = end + 1;
	}

	std::string result = absolute ? "/" : "";
	for (size_t i = 0; i < components.size(); i++) {
		if (i > 0) {
			result += '/';
		}
		result += components[i];
	}
	return result;
}


std::string PercentEncode(std::string_view text, std::string_view extra)
{
	static constexpr std::string_view kAllowed = "-._~!$&'()*+,;=:@/";
	static constexpr char kDigits[] = "0123456789ABCDEF";
	std::string result;
	result.reserve(text.size());
	for (const char ch : text) {
		const unsigned char byte = static_cast<unsigned char>(ch);
		const bool plain = ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9')
			|| kAllowed.find(ch) != std::string_view::npos) && extra.find(ch) == std::string_view::npos;
		if (plain) {
			result += ch;
		} else {
			result += '%';
			result += kDigits[byte >> 4];
			result += kDigits[byte & 0xf];
		}
	}
	return result;
}


std::optional<std::string> PercentDecode(std::string_view text)
{
	auto value = [](char ch) {
		if (ch >= '0' && ch <= '9') {
			return ch - '0';
		}
		if (ch >= 'a' && ch <= 'f') {
			return ch - 'a' + 10;
		}
		return ch - 'A' + 10;
	};
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '%') {
			result += text[i];
			continue;
		}
		if (i + 2 >= text.size() || !IsHex(text[i + 1]) || !IsHex(text[i + 2])) {
			return std::nullopt;
		}
		result += static_cast<char>(value(text[i + 1]) * 16 + value(text[i + 2]));
		i += 2;
	}
	return result;
}


//#pragma mark - Locator

Locator Locator::File(std::string_view path)
{
	Locator locator;
	locator.fKind = LocatorKind::File;
	if (path.starts_with('/')) {
		locator.fPath = NormalizePath(path);
	} else {
		char directory[PATH_MAX];
		const std::string base = getcwd(directory, sizeof(directory)) != nullptr ? directory : "/";
		locator.fPath = NormalizePath(JoinPath(base, path));
	}
	return locator;
}


Locator Locator::GitCommit(std::string_view repository, std::string_view revision, std::string_view path)
{
	Locator locator;
	locator.fKind = LocatorKind::GitCommit;
	locator.fRepository = File(repository).fPath;
	locator.fRevision = revision;
	if (IsFullOid(revision)) {
		std::transform(locator.fRevision.begin(), locator.fRevision.end(), locator.fRevision.begin(), [](char ch) {
			return ch >= 'A' && ch <= 'F' ? static_cast<char>(ch - 'A' + 'a') : ch;
		});
	}
	locator.fPath = NormalizePath(path);
	if (locator.fPath.starts_with('/')) {
		locator.fPath.erase(0, 1);
	}
	return locator;
}


Locator Locator::Sftp(std::string_view user, std::string_view host, int port, std::string_view path)
{
	Locator locator;
	locator.fKind = LocatorKind::Sftp;
	locator.fUser = user;
	locator.fHost = host;
	locator.fPort = port;
	locator.fPath = NormalizePath(path);
	if (!locator.fPath.starts_with('/')) {
		locator.fPath.insert(0, "/");
	}
	return locator;
}


static std::optional<Locator> ParseGit(std::string_view rest)
{
	const size_t question = rest.find('?');
	const std::optional<std::string> repository = PercentDecode(rest.substr(0, question));
	if (!repository.has_value() || !repository->starts_with('/') || question == std::string_view::npos) {
		return std::nullopt;
	}
	std::optional<std::string> commit;
	std::string path;
	std::string_view query = rest.substr(question + 1);
	while (!query.empty()) {
		const size_t amp = query.find('&');
		const std::string_view pair = query.substr(0, amp);
		query = amp == std::string_view::npos ? std::string_view() : query.substr(amp + 1);
		const size_t equal = pair.find('=');
		if (equal == std::string_view::npos) {
			return std::nullopt;
		}
		std::optional<std::string> value = PercentDecode(pair.substr(equal + 1));
		if (!value.has_value()) {
			return std::nullopt;
		}
		const std::string_view key = pair.substr(0, equal);
		if (key == "commit") {
			commit = std::move(value);
		} else if (key == "path") {
			path = std::move(*value);
		} else {
			return std::nullopt;
		}
	}
	if (!commit.has_value() || commit->empty()) {
		return std::nullopt;
	}
	return Locator::GitCommit(*repository, *commit, path);
}


static std::optional<Locator> ParseSftp(std::string_view rest)
{
	const size_t slash = rest.find('/');
	std::string_view authority = rest.substr(0, slash);
	const std::optional<std::string> path = PercentDecode(slash == std::string_view::npos ? "/" : rest.substr(slash));
	if (!path.has_value()) {
		return std::nullopt;
	}
	std::string user;
	const size_t at = authority.rfind('@');
	if (at != std::string_view::npos) {
		std::optional<std::string> decoded = PercentDecode(authority.substr(0, at));
		if (!decoded.has_value() || decoded->empty()) {
			return std::nullopt;
		}
		user = std::move(*decoded);
		authority = authority.substr(at + 1);
	}
	int port = 0;
	const size_t colon = authority.find(':');
	if (colon != std::string_view::npos) {
		const std::string_view digits = authority.substr(colon + 1);
		const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), port);
		if (error != std::errc() || end != digits.data() + digits.size() || port < 1 || port > 65535) {
			return std::nullopt;
		}
		authority = authority.substr(0, colon);
	}
	if (authority.empty()) {
		return std::nullopt;
	}
	return Locator::Sftp(user, authority, port, *path);
}


std::optional<Locator> Locator::Parse(std::string_view text)
{
	const size_t separator = text.find("://");
	if (separator == std::string_view::npos || text.find('#') != std::string_view::npos) {
		return std::nullopt;
	}
	const std::string_view scheme = text.substr(0, separator);
	const std::string_view rest = text.substr(separator + 3);
	if (scheme == "file") {
		if (rest.find('?') != std::string_view::npos) {
			return std::nullopt;
		}
		const std::optional<std::string> path = PercentDecode(rest);
		if (!path.has_value() || !path->starts_with('/')) {
			return std::nullopt;
		}
		return File(*path);
	}
	if (scheme == "git") {
		return ParseGit(rest);
	}
	if (scheme == "sftp") {
		return ParseSftp(rest);
	}
	return std::nullopt;
}


std::string Locator::ToString() const
{
	static constexpr std::string_view kQueryExtra = "&=+";
	switch (fKind) {
		case LocatorKind::File:
			return "file://" + PercentEncode(fPath);
		case LocatorKind::GitCommit:
			return "git://" + PercentEncode(fRepository) + "?commit=" + PercentEncode(fRevision, kQueryExtra)
				+ "&path=" + PercentEncode(fPath, kQueryExtra);
		case LocatorKind::Sftp: {
			std::string result = "sftp://";
			if (!fUser.empty()) {
				result += PercentEncode(fUser, ":@/") + "@";
			}
			result += PercentEncode(fHost, ":@/");
			if (fPort != 0) {
				result += ":" + std::to_string(fPort);
			}
			return result + PercentEncode(fPath);
		}
	}
	return {};
}


bool Locator::IsResolved() const
{
	return fKind != LocatorKind::GitCommit || IsFullOid(fRevision);
}


Locator Locator::WithPath(std::string_view path) const
{
	switch (fKind) {
		case LocatorKind::File:
			return File(path);
		case LocatorKind::GitCommit:
			return GitCommit(fRepository, fRevision, path);
		case LocatorKind::Sftp:
			return Sftp(fUser, fHost, fPort, path);
	}
	return *this;
}


std::string Locator::Leaf() const
{
	const size_t slash = fPath.rfind('/');
	return slash == std::string::npos ? fPath : fPath.substr(slash + 1);
}


Locator Locator::Parent() const
{
	const size_t slash = fPath.rfind('/');
	if (slash == std::string::npos) {
		return WithPath("");
	}
	return WithPath(slash == 0 ? std::string_view("/") : std::string_view(fPath).substr(0, slash));
}


Locator Locator::Sibling(std::string_view name) const
{
	return WithPath(JoinPath(Parent().fPath, name));
}


std::string Locator::DisplayName() const
{
	switch (fKind) {
		case LocatorKind::File:
			return fPath == "/" ? fPath : Leaf() + std::string(kDash) + Parent().fPath;
		case LocatorKind::GitCommit: {
			const std::string revision = IsResolved() ? fRevision.substr(0, kShortOidLength) : fRevision;
			const std::string repository = Locator::File(fRepository).Leaf() + " @ " + revision;
			return fPath.empty() ? repository : Leaf() + std::string(kDash) + repository;
		}
		case LocatorKind::Sftp: {
			const std::string server = (fUser.empty() ? "" : fUser + "@") + fHost;
			return Leaf() + std::string(kDash) + server + ":" + Parent().fPath;
		}
	}
	return {};
}


} // Workspace
