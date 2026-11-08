#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <Locker.h>
#include <Looper.h>
#include <Messenger.h>

#include <CodeEditView/TextModel.h>

#include "Coverage.h"
#include "LlvmCov.h"
#include "Locator.h"
#include "PathMapper.h"


namespace Studio {


// To the watchers of a coverage run: its summary or the coverage of a file
// was loaded, or loading failed.
constexpr uint32 kNoticeCoverageChanged = 'CScv';


// The coverage of an instrumented binary from a merged profile, shown by
// the views following the run. llvm-cov runs in the run's thread. The
// coverage of a document is a decoration set of its model, shared by the
// views; it follows edits, but applies to the source the binary was built
// from. llvm-cov's paths map to locators by the run's PathMapper.
class CoverageRun: public BLooper {
private:
	struct FileEntry {
		std::optional<Coverage::FileCoverage> coverage;
		std::string error;
		bool loading {};
		std::map<CodeEdit::TextModel*, BReference<CodeEdit::DecorationSet>> sets;
	};

	const int32 fId;
	const std::string fName;
	const Coverage::LlvmCov fCov;
	const Workspace::PathMapper fMapper;

	mutable BLocker fLock;
	std::vector<BMessenger> fWatchers;
	std::optional<Coverage::Summary> fSummary;
	std::string fSummaryError;
	bool fSummaryLoading {};
	// By llvm-cov path.
	std::map<std::string, FileEntry> fFiles;

	void LoadSummary();
	void LoadFile(const std::string &path);
	void Apply(const std::string &path);
	void Notify();

public:
	CoverageRun(int32 id, const std::string &binary, const std::string &profile,
		Workspace::PathMapper mapper = Workspace::PathMapper());

	int32 Id() const {return fId;}
	// "Coverage 1: binary".
	const std::string &Name() const {return fName;}

	// The methods below are thread safe; they must not be called with a
	// model locked.

	// Watchers get kNoticeCoverageChanged.
	void AddWatcher(const BMessenger &watcher);
	void RemoveWatcher(const BMessenger &watcher);

	// The coverage of the document as decorations of its model, loaded in
	// the background; none if the run has no path for it.
	BReference<CodeEdit::DecorationSet> DecorationsFor(const Workspace::Locator &locator, CodeEdit::TextModel *model);
	// The coverage of the document for a status line: its summary, or that
	// it is loading or failed, and whether the source is newer than the
	// profile.
	std::string FileStatus(const Workspace::Locator &locator) const;

	std::optional<Coverage::Summary> Summary() const;
	// The summary state for a status line when there is no summary.
	std::string SummaryStatus() const;
	std::optional<Workspace::Locator> LocatorOf(const std::string &path) const;
	// Loads the summary and the coverage of the files shown again.
	void Reload();

	void MessageReceived(BMessage *message) override;
};


} // Studio
