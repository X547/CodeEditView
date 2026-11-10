#include "Document.h"

#include <algorithm>

#include <Autolock.h>
#include <DataIO.h>
#include <Message.h>

#include <CodeEditView/Language.h>

#include "TextSide.h"


namespace Workspace {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


static std::string SavedData(TextModel &model)
{
	BMallocIO stream;
	model.Save(stream);
	return std::string(static_cast<const char*>(stream.Buffer()), stream.BufferLength());
}


static std::string SourceData(Document &document)
{
	TextModel &model = *document.Model();
	ModelLock lock(model);
	return SavedData(model);
}


//#pragma mark - Document

Document::Document(const Locator &locator, bool writable):
	fLocator(locator),
	fModel(new TextModel(), true),
	fWritable(writable),
	fLock("Document")
{
	ModelLock lock(*fModel);
	fModel->SetLanguage(CodeEdit::LanguageForPath(locator.Leaf().c_str()));
}


DocumentStatus Document::Status() const
{
	BAutolock lock(fLock);
	return fStatus;
}


bool Document::IsReadOnly() const
{
	BAutolock lock(fLock);
	return !fWritable || !fStatus.problem.empty();
}


bool Document::IsModified() const
{
	ModelLock lock(*fModel);
	return fModel->IsModified();
}


//#pragma mark - DocumentRegistry

DocumentRegistry::DocumentRegistry(Providers &providers):
	fProviders(providers),
	fLock("DocumentRegistry"),
	fQueue("DocumentRegistry I/O")
{
}


DocumentRegistry::~DocumentRegistry()
{
}


void DocumentRegistry::AddListener(const BMessenger &listener)
{
	BAutolock lock(fLock);
	fListeners.push_back(listener);
}


void DocumentRegistry::RemoveListener(const BMessenger &listener)
{
	BAutolock lock(fLock);
	std::erase(fListeners, listener);
}


void DocumentRegistry::Notify(uint32 what, const Document &document, const std::string &error, bool conflict)
{
	BMessage message(what);
	message.AddString("locator", document.GetLocator().ToString().c_str());
	if (what == kNoticeDocumentSaved) {
		message.AddString("error", error.c_str());
		message.AddBool("conflict", conflict);
	}
	NotifyMessage(message);
}


void DocumentRegistry::NotifyMessage(BMessage &message)
{
	std::vector<BMessenger> listeners;
	{
		BAutolock lock(fLock);
		listeners = fListeners;
	}
	for (const BMessenger &listener : listeners) {
		listener.SendMessage(&message);
	}
}


BReference<Document> DocumentRegistry::Open(const Locator &requested)
{
	Locator locator = requested;
	std::string error;
	const bool untitled = locator.Kind() == LocatorKind::Untitled;
	Provider *provider = fProviders.Find(locator.Kind());
	if (untitled) {
		// No file: empty text, ready at once.
	} else if (provider == nullptr) {
		try {
			fProviders.Get(locator);
		} catch (const ProviderError &providerError) {
			error = providerError.what();
		}
	} else if (!locator.IsResolved()) {
		try {
			locator = provider->Resolve(locator);
		} catch (const ProviderError &providerError) {
			error = providerError.what();
		}
	}

	BReference<Document> document;
	bool load = false;
	{
		BAutolock lock(fLock);
		const auto it = fDocuments.find(locator);
		if (it != fDocuments.end()) {
			document = it->second;
		} else {
			const bool writable = untitled || (provider != nullptr && provider->IsWritable(locator));
			document.SetTo(new Document(locator, writable), true);
			fDocuments.emplace(locator, document);
			if (untitled) {
				document->fStatus.state = DocumentState::Ready;
				document->fLoaded = std::string();
			} else {
				load = error.empty();
			}
		}
	}
	if (!error.empty()) {
		{
			BAutolock lock(document->fLock);
			document->fStatus.state = DocumentState::Failed;
			document->fStatus.error = error;
		}
		Notify(kNoticeDocumentChanged, *document);
		return document;
	}
	if (!load && document->Status().state == DocumentState::Failed) {
		{
			BAutolock lock(document->fLock);
			document->fStatus.state = DocumentState::Loading;
			document->fStatus.error.clear();
		}
		Notify(kNoticeDocumentChanged, *document);
		load = true;
	}
	if (load) {
		Load(document, false);
	}
	return document;
}


BReference<Document> DocumentRegistry::NewUntitled()
{
	BReference<Document> document;
	{
		BAutolock lock(fLock);
		int number = 1;
		while (fDocuments.contains(Locator::Untitled(number))) {
			number++;
		}
		document.SetTo(new Document(Locator::Untitled(number), true), true);
		document->fStatus.state = DocumentState::Ready;
		document->fLoaded = std::string();
		fDocuments.emplace(document->GetLocator(), document);
	}
	Notify(kNoticeDocumentChanged, *document);
	return document;
}


BReference<Document> DocumentRegistry::Find(const Locator &locator)
{
	BAutolock lock(fLock);
	const auto it = fDocuments.find(locator);
	return it != fDocuments.end() ? it->second : BReference<Document>();
}


std::vector<BReference<Document>> DocumentRegistry::All()
{
	BAutolock lock(fLock);
	std::vector<BReference<Document>> documents;
	for (const auto &[locator, document] : fDocuments) {
		documents.push_back(document);
	}
	return documents;
}


void DocumentRegistry::Load(const BReference<Document> &document, bool force)
{
	fQueue.Post([this, document, force] {
		const Locator &locator = document->GetLocator();
		FileData file;
		try {
			// Untitled: nothing to read, reverting empties the text.
			if (locator.Kind() != LocatorKind::Untitled) {
				file = fProviders.Get(locator).Read(locator);
			}
			if (!file.data.has_value() && !document->fWritable) {
				throw ProviderError(locator.DisplayName() + " does not exist");
			}
		} catch (const ProviderError &error) {
			{
				BAutolock lock(document->fLock);
				if (document->fStatus.state == DocumentState::Loading) {
					document->fStatus.state = DocumentState::Failed;
				}
				document->fStatus.error = error.what();
			}
			Notify(kNoticeDocumentChanged, *document);
			return;
		}
		Apply(*document, file, force);
		Notify(kNoticeDocumentChanged, *document);
	});
}


// Called in the I/O thread.
void DocumentRegistry::Apply(Document &document, const FileData &file, bool force)
{
	const std::string bytes = file.data.value_or(std::string());
	bool unchanged;
	{
		BAutolock lock(document.fLock);
		if (!file.data.has_value() && document.fStatus.exists) {
			// Deleted elsewhere: the text stays, saving creates the file
			// again.
			document.fStatus.exists = false;
			document.fStatus.changedElsewhere = true;
			document.fStatus.error.clear();
			document.fVersion.clear();
			return;
		}
		unchanged = document.fLoaded == bytes;
		if (unchanged) {
			document.fStatus.state = DocumentState::Ready;
			document.fStatus.exists = file.data.has_value();
			document.fStatus.error.clear();
			document.fStatus.changedElsewhere = false;
			document.fVersion = file.version;
		}
	}

	TextModel &model = *document.Model();
	std::string problem;
	{
		ModelLock lock(model);
		// The same data again: the text stays unless forced to drop edits.
		if (unchanged && (!force || !model.IsModified())) {
			return;
		}
		if (!force && document.Status().state == DocumentState::Ready && model.IsModified()) {
			BAutolock documentLock(document.fLock);
			document.fStatus.changedElsewhere = true;
			document.fStatus.error.clear();
			return;
		}
		BMemoryIO stream(bytes.data(), bytes.size());
		model.Load(stream);
		model.SetSavePoint();
		// Saving must give back the same bytes, or editing would change
		// lines the user did not touch.
		if (DiffKit::IsBinary(bytes)) {
			problem = "is a binary file";
		} else if (SavedData(model) != bytes) {
			problem = "has mixed line endings";
		}
	}

	BAutolock lock(document.fLock);
	document.fLoaded = bytes;
	document.fVersion = file.version;
	document.fStatus.state = DocumentState::Ready;
	document.fStatus.error.clear();
	document.fStatus.exists = file.data.has_value();
	document.fStatus.changedElsewhere = false;
	document.fStatus.problem = problem;
}


void DocumentRegistry::Reload(Document *document, bool force)
{
	Load(BReference<Document>(document), force);
}


void DocumentRegistry::Save(Document *target, bool overwrite)
{
	BReference<Document> document(target);
	std::string expectedVersion;
	{
		BAutolock lock(document->fLock);
		std::string error;
		if (document->GetLocator().Kind() == LocatorKind::Untitled) {
			error = "The document has no file yet";
		} else if (!document->fWritable) {
			error = "Files of this location are read-only";
		} else if (!document->fStatus.problem.empty()) {
			error = "The file " + document->fStatus.problem;
		} else if (document->fStatus.state != DocumentState::Ready) {
			error = "The file is not loaded";
		} else if (document->fStatus.saving) {
			error = "The file is being saved";
		}
		if (!error.empty()) {
			lock.Unlock();
			Notify(kNoticeDocumentSaved, *document, error);
			return;
		}
		expectedVersion = document->fVersion;
		document->fStatus.saving = true;
	}

	std::string data;
	uint64_t revision;
	{
		TextModel &model = *document->Model();
		ModelLock lock(model);
		data = SavedData(model);
		revision = model.Revision();
	}
	Notify(kNoticeDocumentChanged, *document);

	fQueue.Post([this, document, data = std::move(data), revision, expectedVersion, overwrite] {
		const Locator &locator = document->GetLocator();
		std::string error;
		bool conflict = false;
		std::string version;
		try {
			version = fProviders.Get(locator).Write(locator, data, expectedVersion, overwrite);
		} catch (const VersionConflict &versionConflict) {
			error = versionConflict.what();
			conflict = true;
		} catch (const ProviderError &providerError) {
			error = providerError.what();
		}

		if (error.empty()) {
			TextModel &model = *document->Model();
			ModelLock lock(model);
			// Edits made while writing stay unsaved.
			if (model.Revision() == revision) {
				model.SetSavePoint();
			}
		}
		{
			BAutolock lock(document->fLock);
			document->fStatus.saving = false;
			document->fStatus.error = error;
			if (error.empty()) {
				document->fLoaded = data;
				document->fVersion = version;
				document->fStatus.exists = true;
				document->fStatus.changedElsewhere = false;
			} else if (conflict) {
				document->fStatus.changedElsewhere = true;
			}
		}
		Notify(kNoticeDocumentChanged, *document);
		Notify(kNoticeDocumentSaved, *document, error, conflict);
	});
}


Locator DocumentRegistry::WritableTarget(const Locator &requested)
{
	Provider &provider = fProviders.Get(requested);
	const Locator target = provider.Resolve(requested);
	if (!provider.IsWritable(target)) {
		throw ProviderError("Files of this location are read-only");
	}
	return target;
}


// The document of a file about to be replaced, made if create is set; none
// if it is not open and create is not set.
BReference<Document> DocumentRegistry::Replaced(const Locator &target, bool create)
{
	BReference<Document> document;
	{
		BAutolock lock(fLock);
		const auto it = fDocuments.find(target);
		if (it != fDocuments.end()) {
			document = it->second;
		} else if (create) {
			document.SetTo(new Document(target, true), true);
			fDocuments.emplace(target, document);
		}
	}
	if (document.Get() == nullptr) {
		return document;
	}
	if (document->Status().saving || (document->Status().state == DocumentState::Ready && document->IsModified())) {
		throw ProviderError(target.DisplayName() + " is open with unsaved changes");
	}
	return document;
}


// Writes the file in the background; its document (if any) then holds the
// data. The reply (if valid) gets kNoticeCopySaved. An untitled source
// (if any) of the data counts as saved unless edited meanwhile, and its
// listeners get kNoticeDocumentMoved.
void DocumentRegistry::Replace(const Locator &target, const BReference<Document> &document, std::string data,
	const BMessenger &reply, const BReference<Document> &untitled)
{
	uint64_t untitledRevision = 0;
	if (untitled.Get() != nullptr) {
		ModelLock lock(*untitled->Model());
		untitledRevision = untitled->Model()->Revision();
	}
	if (document.Get() != nullptr) {
		{
			BAutolock lock(document->fLock);
			document->fStatus.saving = true;
			if (document->fStatus.state == DocumentState::Failed) {
				document->fStatus.state = DocumentState::Loading;
			}
		}
		Notify(kNoticeDocumentChanged, *document);
	}

	fQueue.Post([this, target, document, data = std::move(data), reply, untitled, untitledRevision] {
		std::string error;
		FileData file;
		try {
			file.version = fProviders.Get(target).Write(target, data, {}, true);
			file.data = data;
		} catch (const ProviderError &providerError) {
			error = providerError.what();
		}
		if (document.Get() != nullptr) {
			if (error.empty()) {
				Apply(*document, file, true);
			}
			{
				BAutolock lock(document->fLock);
				document->fStatus.saving = false;
				document->fStatus.error = error;
				if (!error.empty() && document->fStatus.state == DocumentState::Loading) {
					document->fStatus.state = DocumentState::Failed;
				}
			}
			Notify(kNoticeDocumentChanged, *document);
			Notify(kNoticeDocumentSaved, *document, error);
		}
		if (untitled.Get() != nullptr && error.empty()) {
			{
				TextModel &model = *untitled->Model();
				ModelLock lock(model);
				if (model.Revision() == untitledRevision) {
					model.SetSavePoint();
				}
			}
			BMessage moved(kNoticeDocumentMoved);
			moved.AddString("locator", untitled->GetLocator().ToString().c_str());
			moved.AddString("to", target.ToString().c_str());
			NotifyMessage(moved);
		}
		if (reply.IsValid()) {
			BMessage message(kNoticeCopySaved);
			message.AddString("locator", target.ToString().c_str());
			message.AddString("error", error.c_str());
			reply.SendMessage(&message);
		}
	});
}


BReference<Document> DocumentRegistry::SaveAs(Document *source, const Locator &requested)
{
	const Locator target = WritableTarget(requested);
	if (target == source->GetLocator()) {
		Save(source, true);
		return BReference<Document>(source);
	}
	BReference<Document> document = Replaced(target, true);
	BReference<Document> untitled;
	if (source->GetLocator().Kind() == LocatorKind::Untitled) {
		untitled.SetTo(source);
	}
	Replace(target, document, SourceData(*source), BMessenger(), untitled);
	return document;
}


void DocumentRegistry::SaveCopy(Document *source, const Locator &requested, const BMessenger &reply)
{
	const Locator target = WritableTarget(requested);
	if (target == source->GetLocator()) {
		Save(source, true);
		return;
	}
	Replace(target, Replaced(target, false), SourceData(*source), reply, BReference<Document>());
}


void DocumentRegistry::CloseUnused()
{
	BAutolock lock(fLock);
	std::erase_if(fDocuments, [](const auto &item) {
		const BReference<Document> &document = item.second;
		if (document->CountReferences() > 1 || document->Status().saving) {
			return false;
		}
		TextModel &model = *document->Model();
		ModelLock modelLock(model);
		return !model.IsModified() && model.SharedMarks().Marks(0, model.Text().NewlineCount()).empty();
	});
}


void DocumentRegistry::WaitIdle()
{
	fQueue.Wait();
}


} // Workspace
