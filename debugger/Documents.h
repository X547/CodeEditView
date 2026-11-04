#pragma once

#include <map>
#include <string>
#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <Referenceable.h>

#include <CodeEditView/TextModel.h>


namespace Debugger {


class SourceDocument: public BReferenceable {
public:
	const std::string path;
	const BReference<CodeEdit::TextModel> model;

	SourceDocument(std::string path, CodeEdit::TextModel *model);
};


// Source files opened by the debugger, one model per path, shared by all
// windows. Documents stay open while the application runs, so their
// breakpoints persist.
class DocumentRegistry {
private:
	BLocker fLock;
	std::map<std::string, BReference<SourceDocument>> fDocuments;
	BMessenger fListener;

public:
	DocumentRegistry();

	static DocumentRegistry &Default();

	// Receives kMsgDocumentOpened.
	void SetListener(const BMessenger &listener);

	BReference<SourceDocument> Find(const std::string &path);
	// Loads the file on first use; nullptr if it cannot be read.
	BReference<SourceDocument> Open(const std::string &path);
	std::vector<BReference<SourceDocument>> All();
};


// Absolute path with "." and ".." resolved; the input if that fails.
std::string NormalizePath(const std::string &path);


} // Debugger
