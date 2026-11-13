#pragma once

#include <stddef.h>

#include <string>
#include <string_view>

#include <CodeEditView/Rope.h>


namespace CodeEdit {


enum class LineEnding {
	LF,
	CRLF,
	CR,
};


// Makes the text of a model from the bytes of a file given in pieces, as
// they are read: CRLF and lone CR become LF, a UTF-8 BOM is removed. Only the
// rope holds the text; there is no contiguous copy of the file.
class TextLoader {
private:
	Rope::Builder fBuilder;
	// The first bytes, until it is known whether they are a BOM.
	std::string fHead;
	bool fHeadDone {};
	bool fPendingCr {};
	bool fFinished {};
	bool fHasBom {};
	size_t fLfCount {};
	size_t fCrlfCount {};
	size_t fCrCount {};

	void AppendText(std::string_view data);

public:
	void Append(std::string_view data);
	// Ends the data; Append() must not be called afterwards.
	void Finish();

	// After Finish(): the text; the loader holds none afterwards.
	Rope TakeText();
	// The line ending most lines had.
	LineEnding DominantLineEnding() const;
	bool HasBom() const {return fHasBom;}
	// Saving the text would not give back the same bytes, as lines end in
	// different ways.
	bool HasMixedLineEndings() const;
};


} // CodeEdit
