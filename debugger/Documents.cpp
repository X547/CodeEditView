#include "Documents.h"

#include <exception>

#include <Autolock.h>
#include <File.h>
#include <Message.h>
#include <Path.h>

#include <CodeEditView/Language.h>

#include "Protocol.h"


namespace Debugger {


SourceDocument::SourceDocument(std::string path, CodeEdit::TextModel *model):
	path(std::move(path)),
	model(model, true)
{
}


DocumentRegistry::DocumentRegistry():
	fLock("DocumentRegistry")
{
}


DocumentRegistry &DocumentRegistry::Default()
{
	static DocumentRegistry registry;
	return registry;
}


void DocumentRegistry::SetListener(const BMessenger &listener)
{
	BAutolock lock(fLock);
	fListener = listener;
}


BReference<SourceDocument> DocumentRegistry::Find(const std::string &path)
{
	BAutolock lock(fLock);
	const auto it = fDocuments.find(NormalizePath(path));
	return it != fDocuments.end() ? it->second : BReference<SourceDocument>();
}


BReference<SourceDocument> DocumentRegistry::Open(const std::string &rawPath)
{
	const std::string path = NormalizePath(rawPath);
	BMessenger listener;
	BReference<SourceDocument> document;
	{
		BAutolock lock(fLock);
		const auto it = fDocuments.find(path);
		if (it != fDocuments.end()) {
			return it->second;
		}
		BFile file(path.c_str(), B_READ_ONLY);
		if (file.InitCheck() != B_OK) {
			return BReference<SourceDocument>();
		}
		CodeEdit::TextModel *model = new CodeEdit::TextModel();
		try {
			CodeEdit::ModelLock modelLock(*model);
			model->Load(file);
			model->SetLanguage(CodeEdit::LanguageForPath(path.c_str()));
		} catch (const std::exception &) {
			delete model;
			return BReference<SourceDocument>();
		}
		document.SetTo(new SourceDocument(path, model), true);
		fDocuments[path] = document;
		listener = fListener;
	}
	BMessage message(kMsgDocumentOpened);
	message.AddString("path", path.c_str());
	listener.SendMessage(&message);
	return document;
}


std::vector<BReference<SourceDocument>> DocumentRegistry::All()
{
	BAutolock lock(fLock);
	std::vector<BReference<SourceDocument>> documents;
	for (const auto &[path, document] : fDocuments) {
		documents.push_back(document);
	}
	return documents;
}


std::string NormalizePath(const std::string &path)
{
	BPath normalized;
	if (path.empty() || normalized.SetTo(path.c_str(), nullptr, true) != B_OK) {
		return path;
	}
	return normalized.Path();
}


} // Debugger
