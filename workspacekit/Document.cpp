#include "Document.h"

#include <algorithm>

#include <Autolock.h>
#include <Message.h>

#include <CodeEditView/Language.h>


namespace Workspace {


using CodeEdit::ModelLock;
using CodeEdit::TextLoader;
using CodeEdit::TextModel;
using CodeEdit::TextSnapshot;


// Data with a NUL byte this early is not edited as text.
static constexpr size_t kBinaryCheckSize = 8000;


// Fingerprints the data of a file and tells whether it is binary; also makes
// its text if asked to.
class ContentSink: public DataSink {
private:
	const bool fMakeText;
	FingerprintBuilder fFingerprint;
	std::optional<TextLoader> fText;
	size_t fChecked {};
	bool fBinary {};

public:
	explicit ContentSink(bool makeText):
		fMakeText(makeText)
	{
		Restart();
	}

	void Restart() final
	{
		fFingerprint = FingerprintBuilder();
		fChecked = 0;
		fBinary = false;
		if (fMakeText) {
			fText.emplace();
		}
	}

	void Append(std::string_view data) final
	{
		fFingerprint.Append(data);
		if (fChecked < kBinaryCheckSize) {
			const std::string_view head = data.substr(0, kBinaryCheckSize - fChecked);
			fBinary = fBinary || head.find('\0') != std::string_view::npos;
			fChecked += head.size();
		}
		if (fText.has_value()) {
			fText->Append(data);
		}
	}

	Fingerprint Result() const {return fFingerprint.Result();}
	bool IsBinary() const {return fBinary;}
	TextLoader *Text() {return fText.has_value() ? &*fText : nullptr;}
};


// Writes the snapshot as a file's data, passing it through the sink too.
static DiffKit::ChunkSource WrittenSource(const TextSnapshot &snapshot, ContentSink &written)
{
	return [&snapshot, &written](const DiffKit::ChunkSink &sink) {
		written.Restart();
		snapshot.Write([&](std::string_view data) {
			written.Append(data);
			sink(data);
		});
	};
}


// Lines of the bookmarks, sorted. Requires the model lock.
static std::vector<size_t> BookmarkLines(const TextModel &model)
{
	std::vector<size_t> lines;
	for (const CodeEdit::MarkInfo &mark : model.SharedMarks().Marks(0, model.Text().NewlineCount())) {
		if (mark.kind == CodeEdit::kMarkBookmark) {
			lines.push_back(mark.line);
		}
	}
	return lines;
}


// Replaces the bookmarks; lines past the end are dropped. Requires the model
// lock.
static void SetBookmarks(TextModel &model, const std::vector<size_t> &lines)
{
	CodeEdit::MarkSet &marks = model.SharedMarks();
	marks.RemoveMarks(CodeEdit::kMarkBookmark);
	for (const size_t line : lines) {
		if (line < model.Text().LineCount()) {
			marks.AddMark(CodeEdit::kMarkBookmark, line, 0, CodeEdit::MarkDeletePolicy::MoveToEditLine);
		}
	}
}


// An empty name is plain text.
static void SetModelLanguage(TextModel &model, const std::string &language)
{
	ModelLock lock(model);
	model.SetLanguage(language.empty() ? nullptr : language.c_str());
}


static TextSnapshot SourceSnapshot(Document &document)
{
	TextModel &model = *document.Model();
	ModelLock lock(model);
	return model.Snapshot();
}


//#pragma mark - Document

Document::Document(const Locator &locator, bool writable, bool hasAttributes):
	fLocator(locator),
	fModel(new TextModel(), true),
	fWritable(writable),
	fHasAttributes(hasAttributes),
	fLock("Document")
{
	SetModelLanguage(*fModel, Language());
}


std::string Document::Language() const
{
	if (fAttributes.options.language.has_value()) {
		return *fAttributes.options.language;
	}
	return LanguageFor(fAttributes.mimeType);
}


std::string Document::LanguageFor(const std::string &mimeType) const
{
	const char *language = CodeEdit::LanguageForMimeType(mimeType);
	if (language == nullptr) {
		language = CodeEdit::LanguageForPath(fLocator.Leaf().c_str());
	}
	return language != nullptr ? language : "";
}


FileAttributes Document::Attributes() const
{
	BAutolock lock(fLock);
	return fAttributes;
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
			const bool hasAttributes = provider != nullptr && provider->HasAttributes(locator);
			document.SetTo(new Document(locator, writable, hasAttributes), true);
			fDocuments.emplace(locator, document);
			if (untitled) {
				document->fStatus.state = DocumentState::Ready;
				document->fLoaded = FingerprintBuilder().Result();
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
		document.SetTo(new Document(Locator::Untitled(number), true, false), true);
		document->fStatus.state = DocumentState::Ready;
		document->fLoaded = FingerprintBuilder().Result();
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


void DocumentRegistry::SetFileSettings(Document *document, const FileAttributes &attributes,
	CodeEdit::LineEnding lineEnding, bool hasBom)
{
	if (!document->IsReadOnly()) {
		TextModel &model = *document->Model();
		ModelLock lock(model);
		model.SetLineEnding(lineEnding);
		model.SetHasBom(hasBom);
	}
	SetFileAttributes(document, attributes);
}


void DocumentRegistry::SetFileAttributes(Document *target, const FileAttributes &attributes)
{
	BReference<Document> document(target);
	std::string language;
	bool write;
	{
		BAutolock lock(document->fLock);
		document->fAttributes = attributes;
		language = document->Language();
		write = document->fHasAttributes && document->fStatus.exists;
	}
	SetModelLanguage(*document->Model(), language);
	Notify(kNoticeDocumentChanged, *document);
	if (write) {
		WriteLater(document, [attributes](Provider &provider, const Locator &locator, const std::string &version) {
			return provider.WriteAttributes(locator, attributes, version);
		});
	}
}


void DocumentRegistry::StoreBookmarks(Document *target)
{
	BReference<Document> document(target);
	{
		BAutolock lock(document->fLock);
		if (!document->fHasAttributes || !document->fStatus.exists || document->fStatus.saving) {
			return;
		}
	}
	std::vector<size_t> bookmarks;
	{
		TextModel &model = *document->Model();
		ModelLock lock(model);
		// Lines of an edited text are not those of the file.
		if (model.IsModified()) {
			return;
		}
		bookmarks = BookmarkLines(model);
	}
	WriteLater(document, [bookmarks](Provider &provider, const Locator &locator, const std::string &version) {
		return provider.WriteBookmarks(locator, bookmarks, version);
	});
}


// In the I/O thread; write gets the version known and returns the one to
// expect. A failure is reported in the status error.
void DocumentRegistry::WriteLater(const BReference<Document> &document, AttributeWriter write)
{
	fQueue.Post([this, document, write = std::move(write)] {
		const Locator &locator = document->GetLocator();
		std::string knownVersion;
		{
			BAutolock lock(document->fLock);
			knownVersion = document->fVersion;
		}
		std::string error;
		std::string version;
		try {
			version = write(fProviders.Get(locator), locator, knownVersion);
		} catch (const ProviderError &providerError) {
			error = providerError.what();
		}
		{
			BAutolock lock(document->fLock);
			if (!error.empty()) {
				document->fStatus.error = error;
			} else if (document->fVersion == knownVersion) {
				document->fVersion = version;
			}
		}
		Notify(kNoticeDocumentChanged, *document);
	});
}


// Text loaded before is only fingerprinted first: the file is read again for
// its text only if that is to replace the model's. Two copies of the text
// exist at once only while replacing it.
void DocumentRegistry::Load(const BReference<Document> &document, bool force)
{
	fQueue.Post([this, document, force] {
		const Locator &locator = document->GetLocator();
		const auto read = [&](ContentSink &content) {
			FileInfo file;
			// Untitled: nothing to read, reverting empties the text.
			if (locator.Kind() != LocatorKind::Untitled) {
				file = fProviders.Get(locator).Read(locator, content);
			}
			if (!file.exists && !document->fWritable) {
				throw ProviderError(locator.DisplayName() + " does not exist");
			}
			return file;
		};
		bool loaded;
		{
			BAutolock lock(document->fLock);
			loaded = document->fLoaded.has_value();
		}
		try {
			ContentSink content(!loaded);
			const FileInfo file = read(content);
			if (!Apply(*document, file, content.Result(), content.Text(), content.IsBinary(), force)) {
				ContentSink text(true);
				const FileInfo textFile = read(text);
				Apply(*document, textFile, text.Result(), text.Text(), text.IsBinary(), force);
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
		Notify(kNoticeDocumentChanged, *document);
	});
}


// Called in the I/O thread. Returns false if the model's text is to be
// replaced but no text is given.
bool DocumentRegistry::Apply(Document &document, const FileInfo &file, const Fingerprint &fingerprint,
	TextLoader *text, bool binary, bool force)
{
	bool unchanged;
	std::string language;
	{
		BAutolock lock(document.fLock);
		if (!file.exists && document.fStatus.exists) {
			// Deleted elsewhere: the text stays, saving creates the file
			// again.
			document.fStatus.exists = false;
			document.fStatus.changedElsewhere = true;
			document.fStatus.error.clear();
			document.fVersion.clear();
			return true;
		}
		// The MIME type takes priority over the path, which gave the language
		// so far, the options over both. Without attributes, the options
		// chosen stay.
		if (file.exists) {
			document.fAttributes.mimeType = file.mimeType;
			if (document.fHasAttributes) {
				document.fAttributes.options = file.options.value_or(FileOptions());
			}
		}
		language = document.Language();
		unchanged = document.fLoaded == fingerprint;
		if (unchanged) {
			document.fStatus.state = DocumentState::Ready;
			document.fStatus.exists = file.exists;
			document.fStatus.error.clear();
			document.fStatus.changedElsewhere = false;
			document.fVersion = file.version;
		}
	}

	TextModel &model = *document.Model();
	SetModelLanguage(model, language);
	std::string problem;
	{
		ModelLock lock(model);
		// The same data again: the text stays unless forced to drop edits.
		if (unchanged && (!force || !model.IsModified())) {
			return true;
		}
		if (!force && document.Status().state == DocumentState::Ready && model.IsModified()) {
			BAutolock documentLock(document.fLock);
			document.fStatus.changedElsewhere = true;
			document.fStatus.error.clear();
			return true;
		}
		if (text == nullptr) {
			return false;
		}
		model.Load(*text);
		model.SetSavePoint();
		// The bookmarks kept with the file are of this text.
		if (document.fHasAttributes) {
			SetBookmarks(model, file.bookmarks);
		}
		// Saving must give back the same bytes, or editing would change
		// lines the user did not touch.
		if (binary) {
			problem = "is a binary file";
		} else if (text->HasMixedLineEndings()) {
			problem = "has mixed line endings";
		}
	}

	BAutolock lock(document.fLock);
	document.fLoaded = fingerprint;
	document.fVersion = file.version;
	document.fStatus.state = DocumentState::Ready;
	document.fStatus.error.clear();
	document.fStatus.exists = file.exists;
	document.fStatus.changedElsewhere = false;
	document.fStatus.problem = problem;
	return true;
}


// Called in the I/O thread: the document's file was written with the
// snapshot, which becomes its text.
void DocumentRegistry::ApplyWritten(Document &document, const TextSnapshot &snapshot, const Fingerprint &fingerprint,
	bool binary, const std::string &version)
{
	{
		TextModel &model = *document.Model();
		ModelLock lock(model);
		model.Load(snapshot);
		model.SetSavePoint();
	}
	BAutolock lock(document.fLock);
	document.fLoaded = fingerprint;
	document.fVersion = version;
	document.fStatus.state = DocumentState::Ready;
	document.fStatus.error.clear();
	document.fStatus.exists = true;
	document.fStatus.changedElsewhere = false;
	document.fStatus.problem = binary ? "is a binary file" : "";
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

	TextSnapshot snapshot;
	uint64_t revision;
	// The bookmarks of the text written.
	std::vector<size_t> bookmarks;
	{
		TextModel &model = *document->Model();
		ModelLock lock(model);
		snapshot = model.Snapshot();
		revision = model.Revision();
		bookmarks = BookmarkLines(model);
	}
	Notify(kNoticeDocumentChanged, *document);

	fQueue.Post([this, document, snapshot, revision, bookmarks, expectedVersion, overwrite] {
		const Locator &locator = document->GetLocator();
		std::string error;
		bool conflict = false;
		std::string version;
		// Failing to write the bookmarks does not fail the save.
		std::string bookmarksError;
		ContentSink written(false);
		try {
			Provider &provider = fProviders.Get(locator);
			version = provider.Write(locator, WrittenSource(snapshot, written), expectedVersion, overwrite);
			if (document->fHasAttributes) {
				try {
					version = provider.WriteBookmarks(locator, bookmarks, version);
				} catch (const ProviderError &providerError) {
					bookmarksError = providerError.what();
				}
			}
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
			document->fStatus.error = error.empty() ? bookmarksError : error;
			if (error.empty()) {
				document->fLoaded = written.Result();
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
			const Provider *provider = fProviders.Find(target.Kind());
			document.SetTo(new Document(target, true, provider != nullptr && provider->HasAttributes(target)), true);
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
void DocumentRegistry::Replace(const Locator &target, const BReference<Document> &document,
	const TextSnapshot &snapshot, const BMessenger &reply, const BReference<Document> &untitled)
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

	fQueue.Post([this, target, document, snapshot, reply, untitled, untitledRevision] {
		std::string error;
		std::string version;
		ContentSink written(false);
		try {
			version = fProviders.Get(target).Write(target, WrittenSource(snapshot, written), {}, true);
		} catch (const ProviderError &providerError) {
			error = providerError.what();
		}
		if (document.Get() != nullptr) {
			if (error.empty()) {
				ApplyWritten(*document, snapshot, written.Result(), written.IsBinary(), version);
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
	Replace(target, document, SourceSnapshot(*source), BMessenger(), untitled);
	return document;
}


void DocumentRegistry::SaveCopy(Document *source, const Locator &requested, const BMessenger &reply)
{
	const Locator target = WritableTarget(requested);
	if (target == source->GetLocator()) {
		Save(source, true);
		return;
	}
	Replace(target, Replaced(target, false), SourceSnapshot(*source), reply, BReference<Document>());
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
		if (model.IsModified()) {
			return false;
		}
		// Bookmarks of a file are kept with it; other documents keep theirs
		// while open.
		for (const CodeEdit::MarkInfo &mark : model.SharedMarks().Marks(0, model.Text().NewlineCount())) {
			if (mark.kind != CodeEdit::kMarkBookmark || !document->fHasAttributes) {
				return false;
			}
		}
		return true;
	});
}


void DocumentRegistry::WaitIdle()
{
	fQueue.Wait();
}


} // Workspace
