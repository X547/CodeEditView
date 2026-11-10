#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <ListItem.h>
#include <OutlineListView.h>

#include "FileDocument.h"
#include "Repository.h"


namespace GitDiff {


// A changed file in one section of the list.
class FileItem: public BStringItem {
private:
	std::string fPath;
	DiffKind fKind;

public:
	FileItem(const std::string &path, DiffKind kind, char letter);

	const std::string &Path() const {return fPath;}
	DiffKind Kind() const {return fKind;}
};


// Changed files in two sections, "Unstaged changes" and "Staged changes",
// like Git GUI. Conflicted files are listed in the unstaged section. Files
// opened without unstaged changes are listed in a third section, "Other
// files", as unstaged (index against working tree).
class FileListView: public BOutlineListView {
private:
	BListItem *fSections[3] {};

public:
	FileListView();

	// Rebuilds the list, keeping the selected file if it is still listed.
	void SetStatus(const std::vector<StatusEntry> &entries, const std::set<std::string> &others);
	std::optional<std::pair<std::string, DiffKind>> Selected() const;
	// Selects a file; returns whether it is listed.
	bool SelectFile(const std::string &path, DiffKind kind);
	// Selects the first file of a section, if any.
	bool SelectFirstFile(DiffKind kind);
};


} // GitDiff
