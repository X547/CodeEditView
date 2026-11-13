#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <Locker.h>
#include <Messenger.h>
#include <Referenceable.h>

#include <CodeEditView/TextModel.h>

#include "Fingerprint.h"
#include "JobQueue.h"
#include "Locator.h"
#include "Provider.h"


namespace Workspace {


// Notices of the DocumentRegistry to its listeners; "locator" (string)
// names the document.
// The status of the document changed (loaded, failed, changed elsewhere,
// saving).
constexpr uint32 kNoticeDocumentChanged = 'WDch';
// A save finished: "error" (string, empty when saved), "conflict" (bool:
// the file was changed elsewhere, nothing was written).
constexpr uint32 kNoticeDocumentSaved = 'WDsv';
// To the reply messenger of DocumentRegistry::SaveCopy(): "locator" (string:
// the copy), "error" (string, empty when saved).
constexpr uint32 kNoticeCopySaved = 'WDcs';
// An untitled document was saved as a file: "to" (string) is its locator.
constexpr uint32 kNoticeDocumentMoved = 'WDmv';


enum class DocumentState {
	Loading,
	Ready,
	// Loading failed; DocumentStatus::error tells why.
	Failed,
};


struct DocumentStatus {
	DocumentState state = DocumentState::Loading;
	// Why loading, reloading or saving last failed; empty if it did not.
	std::string error;
	// The file existed when last read or saved.
	bool exists {};
	// The file changed elsewhere while the text had unsaved edits, or was
	// deleted (then exists is false).
	bool changedElsewhere {};
	// Why the data cannot be edited as text (binary, mixed line endings);
	// empty if it can.
	std::string problem;
	bool saving {};
};


// A file as a text model, with what was last read or saved to tell edits
// and changes elsewhere apart. Made by the DocumentRegistry.
class Document: public BReferenceable {
private:
	const Locator fLocator;
	const BReference<CodeEdit::TextModel> fModel;
	const bool fWritable;

	mutable BLocker fLock;
	DocumentStatus fStatus;
	std::string fVersion;
	// Of the bytes last read or saved; the model holds the only copy of the
	// text.
	std::optional<Fingerprint> fLoaded;

	friend class DocumentRegistry;

public:
	Document(const Locator &locator, bool writable);

	const Locator &GetLocator() const {return fLocator;}
	CodeEdit::TextModel *Model() const {return fModel.Get();}
	DocumentStatus Status() const;
	// The provider cannot write, or the data cannot be edited as text.
	bool IsReadOnly() const;
	// Unsaved edits; locks the model.
	bool IsModified() const;
};


// One document per locator, shared by all views. Files are read and written
// in a background thread; listeners get the notices above. Methods must not
// be called with a model lock held.
class DocumentRegistry {
private:
	Providers &fProviders;
	BLocker fLock;
	std::map<Locator, BReference<Document>> fDocuments;
	std::vector<BMessenger> fListeners;
	// Destroyed first: jobs may still use the members above.
	JobQueue fQueue;

	void Notify(uint32 what, const Document &document, const std::string &error = {}, bool conflict = false);
	void NotifyMessage(BMessage &message);
	void Load(const BReference<Document> &document, bool force);
	bool Apply(Document &document, const FileInfo &file, const Fingerprint &fingerprint, CodeEdit::TextLoader *text,
		bool binary, bool force);
	void ApplyWritten(Document &document, const CodeEdit::TextSnapshot &snapshot, const Fingerprint &fingerprint,
		bool binary, const std::string &version);
	Locator WritableTarget(const Locator &requested);
	BReference<Document> Replaced(const Locator &target, bool create);
	void Replace(const Locator &target, const BReference<Document> &document, const CodeEdit::TextSnapshot &snapshot,
		const BMessenger &reply, const BReference<Document> &untitled);

public:
	explicit DocumentRegistry(Providers &providers = Providers::Default());
	~DocumentRegistry();

	void AddListener(const BMessenger &listener);
	void RemoveListener(const BMessenger &listener);

	// The document of the locator, read in the background the first time
	// (state Loading until then). A Git revision given by name is resolved
	// first, which blocks briefly. Failures give a document in state
	// Failed; opening it again retries.
	BReference<Document> Open(const Locator &locator);
	BReference<Document> Find(const Locator &locator);
	// A new empty document without a file, "Untitled N" with the smallest
	// number not in use; ready at once. Save() fails for it; SaveAs() gives
	// it a file: when written, it counts as saved (and is closed when unused)
	// and listeners get kNoticeDocumentMoved.
	BReference<Document> NewUntitled();
	std::vector<BReference<Document>> All();

	// Reads the file again. With unsaved edits, the text is kept and the
	// document marked changed elsewhere, unless force is set (revert: the
	// edits are dropped even if the file did not change).
	void Reload(Document *document, bool force = false);
	// Writes the text as it is now. Unless overwrite is set, nothing is
	// written if the file changed since it was read or saved (a conflict).
	void Save(Document *document, bool overwrite = false);
	// Writes the text as it is now to another file, replacing it, and
	// returns the document of that file, which then holds the text (undo
	// history starts anew). Throws ProviderError if the location is not
	// writable or its document has unsaved changes.
	BReference<Document> SaveAs(Document *document, const Locator &target);
	// Writes the text as it is now to another file, replacing it; the
	// document stays as it is. A document of that file takes the text. The
	// reply gets kNoticeCopySaved. Throws as SaveAs(); the document's own
	// file is saved as by Save() with overwrite.
	void SaveCopy(Document *document, const Locator &target, const BMessenger &reply);
	// Forgets documents referenced by nobody else, without unsaved edits or
	// shared marks (breakpoints).
	void CloseUnused();
	// Waits until the background work queued so far is done.
	void WaitIdle();
};


} // Workspace
