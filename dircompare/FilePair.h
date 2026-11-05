#pragma once

#include <sys/types.h>

#include <string>

#include <Referenceable.h>

#include <CodeEditView/TextModel.h>

#include <TextSide.h>


namespace DirCompare {


// Files larger than this are not loaded as text.
constexpr off_t kMaxTextFileSize = 20 * 1024 * 1024;


// The left (0) and right (1) version of a file as editable text models. A
// missing file is an empty model; saving creates it.
class FilePair: public BReferenceable {
private:
	std::string fPaths[2];
	DiffKit::TextSide fSides[2];
	bool fExists[2] {};
	// Why a side was not loaded as text; empty if it was.
	std::string fProblems[2];

	void LoadSide(int pane, bool force);

public:
	FilePair(const std::string &leftPath, const std::string &rightPath);

	const std::string &Path(int pane) const {return fPaths[pane];}
	CodeEdit::TextModel *Model(int pane) const {return fSides[pane].Model();}
	bool Exists(int pane) const {return fExists[pane];}
	bool IsModified(int pane) const {return fSides[pane].IsModified();}
	bool ChangedOnDisk(int pane) const {return fSides[pane].ChangedOnDisk();}
	// Why a side cannot be edited; empty if it can.
	const std::string &Problem(int pane) const;

	// Reads both files. Models with unsaved edits keep them (and are marked
	// changed on disk) unless force is set.
	void Load(bool force = false);
	// Writes a side, creating the file and its missing parent directories.
	// Throws DiffKit::FileError.
	void Save(int pane);
};


} // DirCompare
