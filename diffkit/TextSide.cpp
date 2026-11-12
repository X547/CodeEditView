#include "TextSide.h"

#include <DataIO.h>

#include <CodeEditView/Language.h>


namespace DiffKit {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


bool IsBinary(std::string_view data)
{
	return data.substr(0, 8000).find('\0') != std::string_view::npos;
}


TextSide::TextSide(const std::string &path):
	fModel(new TextModel(), true)
{
	ModelLock lock(*fModel);
	fModel->SetLanguage(CodeEdit::LanguageForPath(path));
}


void TextSide::SetLanguage(const char *name)
{
	ModelLock lock(*fModel);
	fModel->SetLanguage(name);
}


bool TextSide::IsModified() const
{
	ModelLock lock(*fModel);
	return fModel->IsModified();
}


std::string TextSide::SavedData() const
{
	BMallocIO stream;
	{
		ModelLock lock(*fModel);
		fModel->Save(stream);
	}
	return std::string(static_cast<const char*>(stream.Buffer()), stream.BufferLength());
}


void TextSide::Load(const std::optional<std::string> &data, bool force)
{
	const std::string bytes = data.value_or(std::string());
	if (fLoaded == bytes) {
		fExists = data.has_value();
		return;
	}
	if (!force && IsModified()) {
		fChangedOnDisk = true;
		return;
	}
	fExists = data.has_value();
	fLoaded = bytes;
	fChangedOnDisk = false;
	{
		ModelLock lock(*fModel);
		BMemoryIO stream(bytes.data(), bytes.size());
		fModel->Load(stream);
		fModel->SetSavePoint();
	}

	// Saving must give back the same bytes, or editing would change lines
	// the user did not touch.
	fProblem.clear();
	if (IsBinary(bytes)) {
		fProblem = "is a binary file";
	} else if (SavedData() != bytes) {
		fProblem = "has mixed line endings";
	}
}


void TextSide::MarkSaved(const std::string &data)
{
	fExists = true;
	fLoaded = data;
	fChangedOnDisk = false;
	ModelLock lock(*fModel);
	fModel->SetSavePoint();
}


} // DiffKit
