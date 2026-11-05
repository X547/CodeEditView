#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ListItem.h>
#include <OutlineListView.h>

#include "DiffView.h"
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
// like Git GUI. Conflicted files are listed in the unstaged section.
class FileListView: public BOutlineListView {
private:
	BListItem *fSections[2] {};

public:
	FileListView();

	// Rebuilds the list, keeping the selected file if it is still listed.
	void SetStatus(const std::vector<StatusEntry> &entries);
	std::optional<std::pair<std::string, DiffKind>> Selected() const;
	// Selects a file; returns whether it is listed.
	bool SelectFile(const std::string &path, DiffKind kind);
	// Selects the first file of a section, if any.
	bool SelectFirstFile(DiffKind kind);
};


} // GitDiff
