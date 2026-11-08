#include "PathMapper.h"


namespace Workspace {


static std::string Join(std::string_view directory, std::string_view relative)
{
	if (relative.empty()) {
		return std::string(directory);
	}
	if (directory.empty()) {
		return std::string(relative);
	}
	std::string result(directory);
	if (result.back() != '/') {
		result += '/';
	}
	return result + std::string(relative);
}


std::optional<std::string> RelativeTo(std::string_view path, std::string_view directory)
{
	if (directory.empty() && !path.starts_with('/')) {
		return std::string(path);
	}
	if (!path.starts_with(directory)) {
		return std::nullopt;
	}
	std::string_view rest = path.substr(directory.size());
	if (rest.empty()) {
		return std::string();
	}
	if (directory.ends_with('/')) {
		return std::string(rest);
	}
	if (!rest.starts_with('/')) {
		return std::nullopt;
	}
	return std::string(rest.substr(1));
}


PathMapper::PathMapper(std::vector<PathRule> rules)
{
	SetRules(std::move(rules));
}


void PathMapper::SetRules(std::vector<PathRule> rules)
{
	for (PathRule &rule : rules) {
		rule.toolDirectory = NormalizePath(rule.toolDirectory);
	}
	fRules = std::move(rules);
}


std::optional<Locator> PathMapper::ToLocator(std::string_view toolPath) const
{
	if (!toolPath.starts_with('/')) {
		return std::nullopt;
	}
	const std::string path = NormalizePath(toolPath);
	for (const PathRule &rule : fRules) {
		if (const std::optional<std::string> relative = RelativeTo(path, rule.toolDirectory)) {
			return rule.target.WithPath(Join(rule.target.Path(), *relative));
		}
	}
	return Locator::File(path);
}


// A locator is in the target directory of a rule if everything but the path
// matches and the path is inside.
std::optional<std::string> PathMapper::ToToolPath(const Locator &locator) const
{
	for (const PathRule &rule : fRules) {
		if (locator.WithPath(rule.target.Path()) != rule.target) {
			continue;
		}
		if (const std::optional<std::string> relative = RelativeTo(locator.Path(), rule.target.Path())) {
			return Join(rule.toolDirectory, *relative);
		}
	}
	if (locator.Kind() == LocatorKind::File) {
		// Unless a rule maps the path elsewhere, the tool sees it as is.
		const std::optional<Locator> back = ToLocator(locator.Path());
		if (back.has_value() && *back == locator) {
			return locator.Path();
		}
	}
	return std::nullopt;
}


} // Workspace
