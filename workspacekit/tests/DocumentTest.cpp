#include <string>

#include <catch2/catch_test_macros.hpp>

#include <Node.h>
#include <NodeInfo.h>

#include <CodeEditView/TextModel.h>

#include "Document.h"
#include "DocumentWatcher.h"
#include "TestSupport.h"


namespace Workspace {


using CodeEdit::ModelLock;


static std::string Text(Document &document)
{
	ModelLock lock(*document.Model());
	return document.Model()->Text().ToString();
}


static void Type(Document &document, std::string_view text)
{
	ModelLock lock(*document.Model());
	document.Model()->Insert(0, text);
}


static std::optional<BMessage> TakeSaved(CollectorHolder &collector, const Document &document)
{
	for (;;) {
		std::optional<BMessage> message = collector->Take(kNoticeDocumentSaved);
		if (!message.has_value() || message->GetString("locator", "") == document.GetLocator().ToString()) {
			return message;
		}
	}
}


TEST_CASE("documents of local files")
{
	TempDirectory directory;
	directory.Write("a.txt", "one\n");
	CollectorHolder collector;
	DocumentRegistry registry;
	registry.AddListener(collector.Messenger());

	BReference<Document> document = registry.Open(Locator::File(directory / "a.txt"));
	REQUIRE(document.Get() != nullptr);
	CHECK(registry.Open(Locator::File(directory / "./sub/../a.txt")) == document);
	CHECK(registry.Find(Locator::File(directory / "a.txt")) == document);
	registry.WaitIdle();
	DocumentStatus status = document->Status();
	CHECK(status.state == DocumentState::Ready);
	CHECK(status.exists);
	CHECK(status.problem.empty());
	CHECK_FALSE(document->IsReadOnly());
	CHECK(Text(*document) == "one\n");
	const std::optional<BMessage> changed = collector->Take(kNoticeDocumentChanged);
	REQUIRE(changed.has_value());
	CHECK(changed->GetString("locator", "") == document->GetLocator().ToString());

	SECTION("save") {
		Type(*document, "zero\n");
		CHECK(document->IsModified());
		registry.Save(document);
		registry.WaitIdle();
		CHECK(directory.Read("a.txt") == "zero\none\n");
		CHECK_FALSE(document->IsModified());
		const std::optional<BMessage> saved = TakeSaved(collector, *document);
		REQUIRE(saved.has_value());
		CHECK(saved->GetString("error", "x") == std::string());
		CHECK_FALSE(document->Status().saving);
	}

	SECTION("reload without edits takes the new data") {
		directory.Write("a.txt", "new data\n");
		registry.Reload(document);
		registry.WaitIdle();
		CHECK(Text(*document) == "new data\n");
		CHECK_FALSE(document->IsModified());
	}

	SECTION("changes elsewhere keep edits and stop saving") {
		directory.Write("a.txt", "changed elsewhere\n");
		Type(*document, "mine\n");
		registry.Reload(document);
		registry.WaitIdle();
		CHECK(document->Status().changedElsewhere);
		CHECK(Text(*document) == "mine\none\n");

		registry.Save(document);
		registry.WaitIdle();
		std::optional<BMessage> saved = TakeSaved(collector, *document);
		REQUIRE(saved.has_value());
		CHECK(saved->GetBool("conflict", false));
		CHECK_FALSE(std::string(saved->GetString("error", "")).empty());
		CHECK(directory.Read("a.txt") == "changed elsewhere\n");
		CHECK(document->IsModified());

		registry.Save(document, true);
		registry.WaitIdle();
		saved = TakeSaved(collector, *document);
		REQUIRE(saved.has_value());
		CHECK_FALSE(saved->GetBool("conflict", true));
		CHECK(directory.Read("a.txt") == "mine\none\n");
		CHECK_FALSE(document->Status().changedElsewhere);
	}

	SECTION("forced reload drops edits") {
		directory.Write("a.txt", "changed elsewhere\n");
		Type(*document, "mine\n");
		registry.Reload(document, true);
		registry.WaitIdle();
		CHECK(Text(*document) == "changed elsewhere\n");
		CHECK_FALSE(document->IsModified());
		CHECK_FALSE(document->Status().changedElsewhere);
	}

	SECTION("revert drops edits of an unchanged file") {
		Type(*document, "mine\n");
		registry.Reload(document);
		registry.WaitIdle();
		CHECK(Text(*document) == "mine\none\n");
		CHECK(document->IsModified());

		registry.Reload(document, true);
		registry.WaitIdle();
		CHECK(Text(*document) == "one\n");
		CHECK_FALSE(document->IsModified());
		CHECK_FALSE(document->Status().changedElsewhere);
	}
}


static std::string LanguageName(Document &document)
{
	ModelLock lock(*document.Model());
	const char *name = document.Model()->LanguageName();
	return name != nullptr ? name : "";
}


TEST_CASE("the MIME type of a file gives the language")
{
	TempDirectory directory;
	directory.Write("Build.txt", "rule A { }\n");
	{
		BNode node((directory / "Build.txt").c_str());
		BNodeInfo info(&node);
		REQUIRE(info.SetType("text/x-jamfile") == B_OK);
	}
	DocumentRegistry registry;
	BReference<Document> document = registry.Open(Locator::File(directory / "Build.txt"));
	registry.WaitIdle();
	CHECK(LanguageName(*document) == "jam");

	// Saving keeps the type and the language.
	Type(*document, "# note\n");
	registry.Save(document);
	registry.WaitIdle();
	CHECK_FALSE(document->IsModified());
	CHECK(LanguageName(*document) == "jam");
	registry.Reload(document, true);
	registry.WaitIdle();
	CHECK(LanguageName(*document) == "jam");

	// Without a type, the path decides.
	directory.Write("Other.txt", "x\n");
	BReference<Document> other = registry.Open(Locator::File(directory / "Other.txt"));
	registry.WaitIdle();
	CHECK(LanguageName(*other).empty());
}


TEST_CASE("file options are kept in the file's attributes")
{
	TempDirectory directory;
	directory.Write("notes", "a\n");
	DocumentRegistry registry;
	BReference<Document> document = registry.Open(Locator::File(directory / "notes"));
	registry.WaitIdle();
	REQUIRE(document->HasAttributes());
	CHECK(document->Attributes().options == FileOptions());
	CHECK(LanguageName(*document).empty());

	FileAttributes attributes;
	attributes.mimeType = "text/x-makefile";
	attributes.options.tabWidth = 8;
	attributes.options.insertSpaces = true;
	attributes.options.indentSize = 2;
	registry.SetFileSettings(document, attributes, CodeEdit::LineEnding::LF, false);
	CHECK(LanguageName(*document) == "make");
	CHECK_FALSE(document->IsModified());
	registry.WaitIdle();
	CHECK(document->Status().error.empty());
	{
		BNode node((directory / "notes").c_str());
		char type[B_MIME_TYPE_LENGTH];
		REQUIRE(BNodeInfo(&node).GetType(type) == B_OK);
		CHECK(std::string(type) == "text/x-makefile");
	}

	// The language chosen wins over the type; saving keeps the attributes.
	attributes.options.language = "jam";
	registry.SetFileSettings(document, attributes, CodeEdit::LineEnding::LF, false);
	registry.WaitIdle();
	CHECK(LanguageName(*document) == "jam");
	Type(*document, "b\n");
	registry.Save(document);
	registry.WaitIdle();
	CHECK(document->Status().error.empty());
	CHECK_FALSE(document->Status().changedElsewhere);

	// A document opened anew reads them.
	document.Unset();
	registry.CloseUnused();
	BReference<Document> reopened = registry.Open(Locator::File(directory / "notes"));
	registry.WaitIdle();
	CHECK(reopened->Attributes().mimeType == "text/x-makefile");
	CHECK(reopened->Attributes().options == attributes.options);
	CHECK(LanguageName(*reopened) == "jam");

	// Plain text chosen.
	attributes.options.language = "";
	registry.SetFileSettings(reopened, attributes, CodeEdit::LineEnding::LF, false);
	CHECK(LanguageName(*reopened).empty());
	registry.WaitIdle();
}


TEST_CASE("line endings and BOM set are unsaved changes")
{
	TempDirectory directory;
	directory.Write("a.txt", "one\ntwo\n");
	DocumentRegistry registry;
	BReference<Document> document = registry.Open(Locator::File(directory / "a.txt"));
	registry.WaitIdle();

	registry.SetFileSettings(document, document->Attributes(), CodeEdit::LineEnding::CRLF, true);
	CHECK(document->IsModified());
	registry.Save(document);
	registry.WaitIdle();
	CHECK_FALSE(document->IsModified());
	CHECK(directory.Read("a.txt") == "\xef\xbb\xbfone\r\ntwo\r\n");
}


TEST_CASE("file options of files without attributes last as long as the document")
{
	DocumentRegistry registry;
	BReference<Document> document = registry.NewUntitled();
	CHECK_FALSE(document->HasAttributes());
	FileAttributes attributes;
	attributes.options.tabWidth = 2;
	registry.SetFileSettings(document, attributes, CodeEdit::LineEnding::LF, false);
	registry.WaitIdle();
	CHECK(document->Attributes().options.tabWidth == 2);
	CHECK(document->Status().error.empty());
}


TEST_CASE("documents of new, binary and mixed files")
{
	TempDirectory directory;
	DocumentRegistry registry;
	CollectorHolder collector;
	registry.AddListener(collector.Messenger());

	BReference<Document> created = registry.Open(Locator::File(directory / "new.txt"));
	registry.WaitIdle();
	CHECK(created->Status().state == DocumentState::Ready);
	CHECK_FALSE(created->Status().exists);
	CHECK(Text(*created).empty());
	Type(*created, "created\n");
	registry.Save(created);
	registry.WaitIdle();
	CHECK(directory.Read("new.txt") == "created\n");
	CHECK(created->Status().exists);

	directory.Write("binary", std::string("a\0b", 3));
	BReference<Document> binary = registry.Open(Locator::File(directory / "binary"));
	directory.Write("mixed.txt", "a\r\nb\n");
	BReference<Document> mixed = registry.Open(Locator::File(directory / "mixed.txt"));
	registry.WaitIdle();
	CHECK(binary->Status().problem == "is a binary file");
	CHECK(binary->IsReadOnly());
	CHECK(mixed->Status().problem == "has mixed line endings");
	CHECK(mixed->IsReadOnly());
	registry.Save(mixed);
	const std::optional<BMessage> saved = TakeSaved(collector, *mixed);
	REQUIRE(saved.has_value());
	CHECK(std::string(saved->GetString("error", "")) == "The file has mixed line endings");
}


TEST_CASE("documents of Git commits")
{
	TestRepository repository;
	repository.Write("a.txt", "one\n");
	const std::string commit = repository.Commit();
	DocumentRegistry registry;
	CollectorHolder collector;
	registry.AddListener(collector.Messenger());

	BReference<Document> document = registry.Open(Locator::GitCommit(repository.Path(), "HEAD", "a.txt"));
	const Locator resolved = Locator::GitCommit(repository.Path(), commit, "a.txt");
	CHECK(document->GetLocator() == resolved);
	CHECK(registry.Open(resolved) == document);
	registry.WaitIdle();
	CHECK(document->Status().state == DocumentState::Ready);
	CHECK(document->IsReadOnly());
	CHECK(Text(*document) == "one\n");
	registry.Save(document);
	const std::optional<BMessage> saved = TakeSaved(collector, *document);
	REQUIRE(saved.has_value());
	CHECK_FALSE(std::string(saved->GetString("error", "")).empty());

	BReference<Document> missing = registry.Open(resolved.WithPath("none.txt"));
	BReference<Document> badRevision = registry.Open(Locator::GitCommit(repository.Path(), "nothing", "a.txt"));
	registry.WaitIdle();
	CHECK(missing->Status().state == DocumentState::Failed);
	CHECK(missing->Status().error.ends_with("does not exist"));
	CHECK(badRevision->Status().state == DocumentState::Failed);
	CHECK_FALSE(badRevision->Status().error.empty());
}


TEST_CASE("unsupported locators and failed documents")
{
	TempDirectory directory;
	DocumentRegistry registry;
	BReference<Document> sftp = registry.Open(Locator::Sftp("", "host", 0, "/a.txt"));
	CHECK(sftp->Status().state == DocumentState::Failed);
	CHECK(sftp->Status().error == "SFTP is not supported yet");
	CHECK(sftp->IsReadOnly());

	// A directory cannot be read; opening again retries.
	directory.Write("dir/a.txt", "a");
	BReference<Document> failed = registry.Open(Locator::File(directory / "dir"));
	registry.WaitIdle();
	CHECK(failed->Status().state == DocumentState::Failed);
	std::filesystem::remove_all(directory / "dir");
	directory.Write("dir", "now a file\n");
	CHECK(registry.Open(Locator::File(directory / "dir")) == failed);
	registry.WaitIdle();
	CHECK(failed->Status().state == DocumentState::Ready);
	CHECK(Text(*failed) == "now a file\n");
}


TEST_CASE("deleted files keep their text")
{
	TempDirectory directory;
	directory.Write("a.txt", "one\n");
	DocumentRegistry registry;
	BReference<Document> document = registry.Open(Locator::File(directory / "a.txt"));
	registry.WaitIdle();
	std::filesystem::remove(directory / "a.txt");
	registry.Reload(document);
	registry.WaitIdle();
	DocumentStatus status = document->Status();
	CHECK_FALSE(status.exists);
	CHECK(status.changedElsewhere);
	CHECK(Text(*document) == "one\n");

	registry.Save(document);
	registry.WaitIdle();
	CHECK(directory.Read("a.txt") == "one\n");
	status = document->Status();
	CHECK(status.exists);
	CHECK_FALSE(status.changedElsewhere);
}


TEST_CASE("save as another file")
{
	TestRepository repository;
	repository.Write("a.txt", "one\n");
	repository.Commit();
	DocumentRegistry registry;
	CollectorHolder collector;
	registry.AddListener(collector.Messenger());
	BReference<Document> source = registry.Open(Locator::GitCommit(repository.Path(), "HEAD", "a.txt"));
	registry.WaitIdle();

	CHECK_THROWS_AS(registry.SaveAs(source, source->GetLocator().WithPath("b.txt")), ProviderError);
	CHECK_THROWS_AS(registry.SaveAs(source, Locator::Sftp("", "host", 0, "/b.txt")), ProviderError);

	BReference<Document> copy = registry.SaveAs(source, Locator::File(repository / "copy.txt"));
	REQUIRE(copy.Get() != nullptr);
	CHECK(registry.Find(Locator::File(repository / "copy.txt")) == copy);
	registry.WaitIdle();
	CHECK(repository.Read("copy.txt") == "one\n");
	CHECK(copy->Status().state == DocumentState::Ready);
	CHECK(copy->Status().exists);
	CHECK(Text(*copy) == "one\n");
	CHECK_FALSE(copy->IsModified());
	CHECK_FALSE(copy->IsReadOnly());
	const std::optional<BMessage> saved = TakeSaved(collector, *copy);
	REQUIRE(saved.has_value());
	CHECK(std::string(saved->GetString("error", "x")).empty());

	// A document with unsaved edits is not replaced.
	Type(*copy, "edit\n");
	CHECK_THROWS_AS(registry.SaveAs(source, copy->GetLocator()), ProviderError);
	CHECK(repository.Read("copy.txt") == "one\n");
}


TEST_CASE("save a copy")
{
	TempDirectory directory;
	directory.Write("a.txt", "one\n");
	DocumentRegistry registry;
	CollectorHolder collector;
	BReference<Document> source = registry.Open(Locator::File(directory / "a.txt"));
	registry.WaitIdle();
	Type(*source, "edit\n");

	const Locator copyLocator = Locator::File(directory / "copy.txt");
	registry.SaveCopy(source, copyLocator, collector.Messenger());
	registry.WaitIdle();
	CHECK(directory.Read("copy.txt") == "edit\none\n");
	CHECK(directory.Read("a.txt") == "one\n");
	CHECK(source->IsModified());
	// No document is made for the copy.
	CHECK(registry.Find(copyLocator).Get() == nullptr);
	std::optional<BMessage> saved = collector->Take(kNoticeCopySaved);
	REQUIRE(saved.has_value());
	CHECK(std::string(saved->GetString("locator", "")) == copyLocator.ToString());
	CHECK(std::string(saved->GetString("error", "x")).empty());

	// An open document of the copy takes the text.
	BReference<Document> copy = registry.Open(copyLocator);
	registry.WaitIdle();
	Type(*source, "more\n");
	registry.SaveCopy(source, copyLocator, collector.Messenger());
	registry.WaitIdle();
	CHECK(directory.Read("copy.txt") == "more\nedit\none\n");
	CHECK(Text(*copy) == "more\nedit\none\n");
	CHECK_FALSE(copy->IsModified());
	CHECK(collector->Take(kNoticeCopySaved).has_value());

	// Not over unsaved edits.
	Type(*copy, "mine\n");
	CHECK_THROWS_AS(registry.SaveCopy(source, copyLocator, collector.Messenger()), ProviderError);

	// Failures are told.
	registry.SaveCopy(source, Locator::File(directory / "none/copy.txt"), collector.Messenger());
	saved = collector->Take(kNoticeCopySaved);
	REQUIRE(saved.has_value());
	CHECK_FALSE(std::string(saved->GetString("error", "")).empty());
}


TEST_CASE("untitled documents")
{
	TempDirectory directory;
	DocumentRegistry registry;
	CollectorHolder collector;
	registry.AddListener(collector.Messenger());

	BReference<Document> first = registry.NewUntitled();
	BReference<Document> second = registry.NewUntitled();
	CHECK(first->GetLocator() == Locator::Untitled(1));
	CHECK(second->GetLocator() == Locator::Untitled(2));
	CHECK(first->Status().state == DocumentState::Ready);
	CHECK_FALSE(first->Status().exists);
	CHECK_FALSE(first->IsReadOnly());
	CHECK(Text(*first).empty());
	CHECK(registry.Open(Locator::Untitled(1)) == first);

	// No file to save to.
	Type(*first, "text\n");
	registry.Save(first);
	std::optional<BMessage> saved = TakeSaved(collector, *first);
	REQUIRE(saved.has_value());
	CHECK_FALSE(std::string(saved->GetString("error", "")).empty());
	CHECK(first->IsModified());

	SECTION("revert empties the text") {
		registry.Reload(first, true);
		registry.WaitIdle();
		CHECK(Text(*first).empty());
		CHECK_FALSE(first->IsModified());
	}

	SECTION("save as gives a file") {
		const Locator target = Locator::File(directory / "a.txt");
		BReference<Document> document = registry.SaveAs(first, target);
		registry.WaitIdle();
		CHECK(document->GetLocator() == target);
		CHECK(directory.Read("a.txt") == "text\n");
		CHECK(Text(*document) == "text\n");
		// The untitled document counts as saved and goes when unused.
		CHECK_FALSE(first->IsModified());
		const std::optional<BMessage> moved = collector->Take(kNoticeDocumentMoved);
		REQUIRE(moved.has_value());
		CHECK(std::string(moved->GetString("locator", "")) == Locator::Untitled(1).ToString());
		CHECK(std::string(moved->GetString("to", "")) == target.ToString());
		first.Unset();
		registry.CloseUnused();
		CHECK(registry.Find(Locator::Untitled(1)).Get() == nullptr);

		// The smallest free number is taken again.
		CHECK(registry.NewUntitled()->GetLocator() == Locator::Untitled(1));
	}
}


// Waits up to two seconds for the condition, which is checked after the
// registry's background work.
template<typename Condition>
static bool WaitFor(DocumentRegistry &registry, Condition condition)
{
	const bigtime_t deadline = system_time() + 2000000;
	for (;;) {
		registry.WaitIdle();
		if (condition()) {
			return true;
		}
		if (system_time() > deadline) {
			return false;
		}
		snooze(20000);
	}
}


TEST_CASE("watched documents follow their files")
{
	TempDirectory directory;
	directory.Write("a.txt", "one\n");
	DocumentRegistry registry;
	DocumentWatcher *watcher = new DocumentWatcher(registry);
	watcher->Run();
	BReference<Document> document = registry.Open(Locator::File(directory / "a.txt"));
	registry.WaitIdle();
	watcher->Update();
	BMessage reply;
	BMessenger(watcher).SendMessage('ping', &reply);

	directory.Write("a.txt", "two two\n");
	CHECK(WaitFor(registry, [&] {return Text(*document) == "two two\n";}));

	// Replaced by a rename, as editors save.
	directory.Write("a.tmp", "three\n");
	std::filesystem::rename(directory / "a.tmp", directory / "a.txt");
	CHECK(WaitFor(registry, [&] {return Text(*document) == "three\n";}));
	directory.Write("a.txt", "four\n");
	CHECK(WaitFor(registry, [&] {return Text(*document) == "four\n";}));

	Type(*document, "mine\n");
	directory.Write("a.txt", "five five\n");
	CHECK(WaitFor(registry, [&] {return document->Status().changedElsewhere;}));
	CHECK(Text(*document) == "mine\nfour\n");

	std::filesystem::remove(directory / "a.txt");
	CHECK(WaitFor(registry, [&] {return !document->Status().exists;}));
	CHECK(Text(*document) == "mine\nfour\n");

	watcher->Lock();
	watcher->Quit();
}


TEST_CASE("unused documents are closed")
{
	TempDirectory directory;
	directory.Write("a.txt", "a\nb\n");
	directory.Write("b.txt", "b\n");
	directory.Write("c.txt", "c\n");
	DocumentRegistry registry;
	const Locator a = Locator::File(directory / "a.txt");
	const Locator b = Locator::File(directory / "b.txt");
	const Locator c = Locator::File(directory / "c.txt");
	BReference<Document> held = registry.Open(a);
	registry.Open(b);
	{
		BReference<Document> marked = registry.Open(c);
		registry.WaitIdle();
		ModelLock lock(*marked->Model());
		marked->Model()->SharedMarks().AddMark(CodeEdit::kMarkBreakpoint, 0);
	}
	registry.WaitIdle();
	registry.CloseUnused();
	CHECK(registry.Find(a).Get() != nullptr);
	CHECK(registry.Find(b).Get() == nullptr);
	CHECK(registry.Find(c).Get() != nullptr);
	CHECK(registry.All().size() == 2);
}


} // Workspace
