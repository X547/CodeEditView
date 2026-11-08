#include <string>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "Document.h"
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
