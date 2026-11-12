#include "FilePair.h"

#include <filesystem>

#include <CodeEditView/Language.h>
#include <FileIO.h>

#include "Compare.h"


namespace DirCompare {


using DiffKit::FileError;


FilePair::FilePair(const std::string &leftPath, const std::string &rightPath):
	fPaths {leftPath, rightPath},
	fSides {DiffKit::TextSide(leftPath), DiffKit::TextSide(rightPath)}
{
}


const std::string &FilePair::Problem(int pane) const
{
	return fProblems[pane].empty() ? fSides[pane].Problem() : fProblems[pane];
}


void FilePair::LoadSide(int pane, bool force)
{
	DiffKit::TextSide &side = fSides[pane];
	const EntryInfo info = ReadEntryInfo(fPaths[pane]);
	fExists[pane] = info.type != EntryType::None;
	fProblems[pane].clear();
	switch (info.type) {
		case EntryType::None:
			side.Load(std::nullopt, force);
			return;
		case EntryType::File:
			if (info.size > kMaxTextFileSize) {
				fProblems[pane] = "is too large";
			} else {
				try {
					side.Load(DiffKit::ReadFile(fPaths[pane]), force);
					// The MIME type may name the language.
					side.SetLanguage(CodeEdit::LanguageForFile(fPaths[pane].c_str()));
					return;
				} catch (const FileError &e) {
					fProblems[pane] = std::string("cannot be read: ") + e.what();
				}
			}
			break;
		case EntryType::Directory:
			fProblems[pane] = "is a directory";
			break;
		case EntryType::Symlink:
			fProblems[pane] = "is a symbolic link";
			break;
		case EntryType::Unreadable:
			fProblems[pane] = "cannot be read";
			break;
		default:
			fProblems[pane] = "is not a regular file";
			break;
	}
	// Not text: nothing to show or edit.
	side.Load(std::nullopt, true);
}


void FilePair::Load(bool force)
{
	LoadSide(0, force);
	LoadSide(1, force);
}


void FilePair::Save(int pane)
{
	const std::string data = fSides[pane].SavedData();
	const std::filesystem::path parent = std::filesystem::path(fPaths[pane]).parent_path();
	if (!parent.empty()) {
		std::error_code error;
		std::filesystem::create_directories(parent, error);
		if (error) {
			throw FileError("Cannot create " + parent.string() + ": " + error.message());
		}
	}
	DiffKit::WriteFile(fPaths[pane], data);
	fSides[pane].MarkSaved(data);
	fExists[pane] = true;
}


} // DirCompare
