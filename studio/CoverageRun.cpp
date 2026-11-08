#include "CoverageRun.h"

#include <sys/stat.h>

#include <algorithm>
#include <exception>

#include <Autolock.h>
#include <InterfaceDefs.h>
#include <Path.h>


namespace Studio {


using CodeEdit::DecorationSet;
using CodeEdit::ModelLock;
using CodeEdit::TextModel;


static constexpr uint32 kMsgLoadSummary = 'CRls';
// "path" (string).
static constexpr uint32 kMsgLoadFile = 'CRlf';
static constexpr uint32 kMsgApply = 'CRap';
static constexpr uint32 kMsgReload = 'CRrl';


static bool IsNewer(const std::string &path, const std::string &than)
{
	struct stat pathStat;
	struct stat thanStat;
	return stat(path.c_str(), &pathStat) == 0 && stat(than.c_str(), &thanStat) == 0
		&& pathStat.st_mtime > thanStat.st_mtime;
}


static std::string RunName(int32 id, const std::string &binary)
{
	BPath path(binary.c_str());
	return "Coverage " + std::to_string(id) + ": " + (path.Leaf() != nullptr ? path.Leaf() : binary.c_str());
}


CoverageRun::CoverageRun(int32 id, const std::string &binary, const std::string &profile,
	Workspace::PathMapper mapper):
	BLooper("CoverageRun"),
	fId(id),
	fName(RunName(id, binary)),
	fCov(binary, profile),
	fMapper(std::move(mapper)),
	fLock("CoverageRun")
{
	fSummaryLoading = true;
	PostMessage(kMsgLoadSummary);
}


void CoverageRun::AddWatcher(const BMessenger &watcher)
{
	BAutolock lock(fLock);
	if (std::find(fWatchers.begin(), fWatchers.end(), watcher) == fWatchers.end()) {
		fWatchers.push_back(watcher);
	}
}


void CoverageRun::RemoveWatcher(const BMessenger &watcher)
{
	BAutolock lock(fLock);
	std::erase(fWatchers, watcher);
}


void CoverageRun::Notify()
{
	std::vector<BMessenger> watchers;
	{
		BAutolock lock(fLock);
		std::erase_if(fWatchers, [](const BMessenger &watcher) {
			return !watcher.IsValid();
		});
		watchers = fWatchers;
	}
	BMessage notice(kNoticeCoverageChanged);
	notice.AddInt32("coverage", fId);
	for (const BMessenger &watcher : watchers) {
		watcher.SendMessage(&notice);
	}
}


BReference<DecorationSet> CoverageRun::DecorationsFor(const Workspace::Locator &locator, TextModel *model)
{
	const std::optional<std::string> path = fMapper.ToToolPath(locator);
	if (!path.has_value()) {
		return {};
	}
	BAutolock lock(fLock);
	FileEntry &entry = fFiles[*path];
	// Sets no view shows any more.
	std::erase_if(entry.sets, [](const auto &item) {
		return item.second->CountReferences() == 1;
	});
	BReference<DecorationSet> &set = entry.sets[model];
	if (set.Get() != nullptr) {
		return set;
	}
	set.SetTo(new DecorationSet(*model), true);
	BMessage request(entry.coverage.has_value() ? kMsgApply : kMsgLoadFile);
	request.AddString("path", path->c_str());
	if (!entry.coverage.has_value() && !entry.loading) {
		entry.loading = true;
		entry.error.clear();
		PostMessage(&request);
	} else if (entry.coverage.has_value()) {
		PostMessage(&request);
	}
	return set;
}


std::string CoverageRun::FileStatus(const Workspace::Locator &locator) const
{
	const std::optional<std::string> path = fMapper.ToToolPath(locator);
	if (!path.has_value()) {
		return "no coverage for this location";
	}
	std::string status;
	{
		BAutolock lock(fLock);
		const auto it = fFiles.find(*path);
		if (it != fFiles.end() && it->second.loading) {
			status = "loading coverage" B_UTF8_ELLIPSIS;
		} else if (it != fFiles.end() && !it->second.error.empty()) {
			status = "llvm-cov: " + it->second.error;
		} else if (fSummary.has_value()) {
			status = "no coverage data";
			for (const Coverage::FileSummary &file : fSummary->files) {
				if (file.path == *path) {
					status = Coverage::FormatSummary(file);
					break;
				}
			}
		}
	}
	if (locator.Kind() == Workspace::LocatorKind::File && IsNewer(locator.Path(), fCov.Profile())) {
		status += " \xc2\xb7 the source is newer than the profile";
	}
	return status;
}


std::optional<Coverage::Summary> CoverageRun::Summary() const
{
	BAutolock lock(fLock);
	return fSummary;
}


std::string CoverageRun::SummaryStatus() const
{
	BAutolock lock(fLock);
	if (fSummaryLoading) {
		return "Loading coverage" B_UTF8_ELLIPSIS;
	}
	if (!fSummaryError.empty()) {
		return "llvm-cov: " + fSummaryError;
	}
	if (fSummary.has_value()) {
		return "Total: " + Coverage::FormatSummary(fSummary->totals) + " \xc2\xb7 "
			+ std::to_string(fSummary->files.size()) + " files";
	}
	return {};
}


std::optional<Workspace::Locator> CoverageRun::LocatorOf(const std::string &path) const
{
	return fMapper.ToLocator(path);
}


void CoverageRun::Reload()
{
	PostMessage(kMsgReload);
}


//#pragma mark - Run thread

void CoverageRun::LoadSummary()
{
	std::optional<Coverage::Summary> summary;
	std::string error;
	try {
		summary = fCov.LoadSummary();
	} catch (const std::exception &exception) {
		error = exception.what();
	}
	{
		BAutolock lock(fLock);
		fSummary = std::move(summary);
		fSummaryError = error;
		fSummaryLoading = false;
	}
	Notify();
}


void CoverageRun::LoadFile(const std::string &path)
{
	std::optional<Coverage::FileCoverage> coverage;
	std::string error;
	try {
		coverage = fCov.LoadFile(path);
	} catch (const std::exception &exception) {
		error = exception.what();
	}
	{
		BAutolock lock(fLock);
		FileEntry &entry = fFiles[path];
		entry.coverage = std::move(coverage);
		entry.error = error;
		entry.loading = false;
	}
	Apply(path);
	Notify();
}


// Sets the decorations of every model of the file from its coverage.
void CoverageRun::Apply(const std::string &path)
{
	std::optional<Coverage::FileCoverage> coverage;
	std::vector<BReference<DecorationSet>> sets;
	{
		BAutolock lock(fLock);
		const auto it = fFiles.find(path);
		if (it == fFiles.end() || !it->second.coverage.has_value()) {
			return;
		}
		coverage = it->second.coverage;
		for (const auto &[model, set] : it->second.sets) {
			sets.push_back(set);
		}
	}
	const Coverage::Colors colors = Coverage::DefaultColors();
	for (const BReference<DecorationSet> &set : sets) {
		TextModel &model = set->Model();
		ModelLock lock(model);
		const CodeEdit::Rope &text = model.Text();
		const std::vector<Coverage::LineState> lines = Coverage::ComputeLineStates(coverage->segments, text.LineCount());
		set->SetDecorations(Coverage::MakeDecorations(*coverage, lines, text, colors));
	}
}


void CoverageRun::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgLoadSummary:
			LoadSummary();
			break;
		case kMsgLoadFile:
			LoadFile(message->GetString("path", ""));
			break;
		case kMsgApply:
			Apply(message->GetString("path", ""));
			break;
		case kMsgReload: {
			std::vector<std::string> paths;
			{
				BAutolock lock(fLock);
				fSummaryLoading = true;
				for (auto &[path, entry] : fFiles) {
					entry.coverage.reset();
					entry.error.clear();
					entry.loading = !entry.sets.empty();
					if (entry.loading) {
						paths.push_back(path);
					}
				}
			}
			Notify();
			LoadSummary();
			for (const std::string &path : paths) {
				LoadFile(path);
			}
			break;
		}
		default:
			BLooper::MessageReceived(message);
			break;
	}
}


} // Studio
