#pragma once

#include <compare>
#include <optional>
#include <string>
#include <string_view>


namespace Workspace {


enum class LocatorKind {
	// A local file: an absolute path.
	File,
	// A file in a Git repository at a commit (read-only).
	GitCommit,
	// A file on an SFTP server.
	Sftp,
};


// Where a document comes from. A value; the string form (ToString(), Parse())
// is used in messages, settings and for display. Locators made by the
// factories and Parse() are canonical (paths normalized), so equal locators
// mean the same file, except for a Git revision given by name until it is
// resolved (GitProvider::Resolve()).
//
// String forms:
//   file:///boot/home/src/a.cpp
//   git:///boot/home/src/repo?commit=<40 hex>&path=src/a.cpp
//   sftp://user@host:22/home/user/a.cpp
class Locator {
private:
	LocatorKind fKind = LocatorKind::File;
	// File and Sftp: absolute path. GitCommit: path in the tree, relative,
	// empty for the root.
	std::string fPath;
	// GitCommit: the repository directory (absolute).
	std::string fRepository;
	// GitCommit: a full commit id (lowercase hex) or a revision name.
	std::string fRevision;
	// Sftp.
	std::string fUser;
	std::string fHost;
	// 0: the default (from the SSH configuration, else 22).
	int fPort {};

public:
	Locator() = default;

	static Locator File(std::string_view path);
	static Locator GitCommit(std::string_view repository, std::string_view revision, std::string_view path);
	static Locator Sftp(std::string_view user, std::string_view host, int port, std::string_view path);
	// None for a string that is not a locator of a known kind.
	static std::optional<Locator> Parse(std::string_view text);

	std::string ToString() const;

	LocatorKind Kind() const {return fKind;}
	const std::string &Path() const {return fPath;}
	const std::string &Repository() const {return fRepository;}
	const std::string &Revision() const {return fRevision;}
	const std::string &User() const {return fUser;}
	const std::string &Host() const {return fHost;}
	int Port() const {return fPort;}

	// A Git revision is a full commit id; always true for other kinds.
	bool IsResolved() const;
	// The same file at another path of the same place (repository, commit,
	// server).
	Locator WithPath(std::string_view path) const;
	// The last path component; empty for a root.
	std::string Leaf() const;
	// The directory containing the file; the locator itself for a root.
	Locator Parent() const;
	// A file in the same directory.
	Locator Sibling(std::string_view name) const;
	// "a.cpp — repo @ 1a2b3c4", "a.cpp — host:/home/user", or the path of a
	// local file.
	std::string DisplayName() const;

	bool operator==(const Locator &other) const = default;
	auto operator<=>(const Locator &other) const = default;
};


// Normalizes a path lexically: no empty, "." or ".." components (".." at
// the root stays at the root) and no trailing '/'. Absolute paths stay
// absolute; a relative path that normalizes to nothing becomes "".
std::string NormalizePath(std::string_view path);

// Percent-encoding of the bytes outside the URI path character set
// (unreserved, sub-delimiters, ':', '@', '/'); extra characters are encoded
// too.
std::string PercentEncode(std::string_view text, std::string_view extra = {});
// None for a malformed escape.
std::optional<std::string> PercentDecode(std::string_view text);


} // Workspace
