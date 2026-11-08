#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Locator.h"


namespace Workspace {


// A directory of a tool (debugger, llvm-cov) and the directory locator its
// files are found at.
struct PathRule {
	// Absolute; normalized when set.
	std::string toolDirectory;
	Locator target;

	bool operator==(const PathRule &other) const = default;
};


// Maps the paths a tool reports (of the machine where the program was built
// or runs) to locators and back. The first rule whose directory contains the
// path applies; without one, absolute paths are local files.
class PathMapper {
private:
	std::vector<PathRule> fRules;

public:
	PathMapper() = default;
	explicit PathMapper(std::vector<PathRule> rules);

	const std::vector<PathRule> &Rules() const {return fRules;}
	void SetRules(std::vector<PathRule> rules);

	// None for a relative path.
	std::optional<Locator> ToLocator(std::string_view toolPath) const;
	// The tool path of a locator; none if no rule maps to it (and it is not
	// a local file).
	std::optional<std::string> ToToolPath(const Locator &locator) const;
};


// The part of path after directory if directory contains it ("" for the
// directory itself); compares whole components. An empty relative
// directory contains every relative path.
std::optional<std::string> RelativeTo(std::string_view path, std::string_view directory);


} // Workspace
