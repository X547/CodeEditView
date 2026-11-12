#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <Referenceable.h>

#include <CodeEditView/TextModel.h>


namespace DiffKit {


// One version of a file as an editable text model, with what was last
// loaded or saved, to tell edits and changes on disk apart.
class TextSide {
private:
	BReference<CodeEdit::TextModel> fModel;
	bool fExists {};
	// Bytes last loaded or saved; reloading skips unchanged data.
	std::optional<std::string> fLoaded;
	// The data changed while the model had unsaved edits.
	bool fChangedOnDisk {};
	// Why the loaded data cannot be edited as text; empty if it can.
	std::string fProblem;

public:
	// The language of the model comes from path, until SetLanguage().
	explicit TextSide(const std::string &path);

	// A language of TextModel::SetLanguage(), such as one from the file's
	// MIME type.
	void SetLanguage(const char *name);

	CodeEdit::TextModel *Model() const {return fModel.Get();}
	bool Exists() const {return fExists;}
	bool IsModified() const;
	bool ChangedOnDisk() const {return fChangedOnDisk;}
	const std::string &Problem() const {return fProblem;}

	// Loads data (none: the version does not exist, the model is empty).
	// Unsaved edits are kept, and the side is marked changed on disk, unless
	// force is set. Data that does not save back to the same bytes (binary,
	// mixed line endings) sets a problem.
	void Load(const std::optional<std::string> &data, bool force);
	// The model's text with its line endings and BOM, as it would be saved.
	std::string SavedData() const;
	// Records data as saved: the side exists and the model is unmodified.
	void MarkSaved(const std::string &data);
};


// Git's rule: a NUL byte in the first 8000 bytes.
bool IsBinary(std::string_view data);


} // DiffKit
