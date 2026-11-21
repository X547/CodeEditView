#include <stdio.h>

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <DataIO.h>
#include <Looper.h>
#include <OS.h>

#include <CodeEditView/TextModel.h>


namespace CodeEdit {


static constexpr bigtime_t kDeliveryTimeout = 1000000;
static constexpr bigtime_t kSilenceTimeout = 100000;


class NotificationCounter: public BLooper {
private:
	sem_id fSem;
	std::atomic<int32_t> fCount {};

public:
	NotificationCounter():
		BLooper("NotificationCounter"),
		fSem(create_sem(0, "notifications"))
	{
	}

	~NotificationCounter() override
	{
		delete_sem(fSem);
	}

	void MessageReceived(BMessage *message) final
	{
		if (message->what != kMsgModelChanged) {
			BLooper::MessageReceived(message);
			return;
		}
		fCount++;
		release_sem(fSem);
	}

	int32_t Count() const {return fCount;}

	bool Wait(bigtime_t timeout)
	{
		return acquire_sem_etc(fSem, 1, B_RELATIVE_TIMEOUT, timeout) == B_OK;
	}
};


// Owns a running NotificationCounter and quits it on destruction.
class CounterHolder {
private:
	NotificationCounter *fCounter;

public:
	CounterHolder():
		fCounter(new NotificationCounter())
	{
		fCounter->Run();
	}

	~CounterHolder()
	{
		Quit();
	}

	void Quit()
	{
		if (fCounter == nullptr) {
			return;
		}
		fCounter->Lock();
		fCounter->Quit();
		fCounter = nullptr;
	}

	NotificationCounter *operator->() const {return fCounter;}
	BMessenger Messenger() const {return BMessenger(fCounter);}
};


static std::string Text(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


static size_t MarkLine(TextModel &model, MarkId id)
{
	ModelLock lock(model);
	const std::optional<MarkInfo> info = model.SharedMarks().FindMark(id);
	REQUIRE(info.has_value());
	return info->line;
}


static bool HasMark(TextModel &model, MarkId id)
{
	ModelLock lock(model);
	return model.SharedMarks().FindMark(id).has_value();
}


static void Type(TextModel &model, ObserverId owner, size_t offset, std::string_view text)
{
	ModelLock lock(model);
	UndoScript script(model, "Typing", owner, {offset, offset}, EditKind::Typing);
	model.Insert(offset, text);
}


static void Backspace(TextModel &model, ObserverId owner, size_t caret)
{
	ModelLock lock(model);
	UndoScript script(model, "Delete", owner, {caret, caret}, EditKind::Backspace);
	model.Erase(caret - 1, 1);
}


TEST_CASE("EditRecord maps offsets")
{
	EditRecord insertion;
	insertion.offset = 5;
	insertion.newLength = 3;
	CHECK(insertion.MapOffset(4, Gravity::Left) == 4);
	CHECK(insertion.MapOffset(5, Gravity::Left) == 5);
	CHECK(insertion.MapOffset(5, Gravity::Right) == 8);
	CHECK(insertion.MapOffset(6, Gravity::Left) == 9);

	EditRecord replacement;
	replacement.offset = 5;
	replacement.oldLength = 4;
	replacement.newLength = 2;
	CHECK(replacement.MapOffset(5, Gravity::Right) == 5);
	CHECK(replacement.MapOffset(9, Gravity::Left) == 7);
	CHECK(replacement.MapOffset(7, Gravity::Left) == 5);
	CHECK(replacement.MapOffset(7, Gravity::Right) == 7);
	CHECK(replacement.MapOffset(10, Gravity::Left) == 8);
}


TEST_CASE("observer receives edit records")
{
	TextModel model;
	CounterHolder counter;
	ObserverId id;
	{
		ModelLock lock(model);
		model.SetText("ab\ncd\n");
		id = model.AddObserver(counter.Messenger());
		model.Replace(4, 1, "X\nY");
	}
	REQUIRE(counter->Wait(kDeliveryTimeout));

	ModelLock lock(model);
	const ModelChanges changes = model.Sync(id);
	CHECK_FALSE(changes.reset);
	CHECK(changes.revision == model.Revision());
	REQUIRE(changes.edits.size() == 1);
	const EditRecord &edit = changes.edits.front();
	CHECK(edit.offset == 4);
	CHECK(edit.oldLength == 1);
	CHECK(edit.newLength == 3);
	CHECK(edit.start == TextPoint {1, 1});
	CHECK(edit.oldEnd == TextPoint {1, 2});
	CHECK(edit.newEnd == TextPoint {2, 1});
	CHECK(model.Text().ToString() == "ab\ncX\nY\n");
}


TEST_CASE("notifications are coalesced until Sync")
{
	TextModel model;
	CounterHolder counter;
	ObserverId id;
	{
		ModelLock lock(model);
		id = model.AddObserver(counter.Messenger());
	}
	for (int i = 0; i < 3; i++) {
		ModelLock lock(model);
		model.Insert(0, "x");
	}
	REQUIRE(counter->Wait(kDeliveryTimeout));
	CHECK_FALSE(counter->Wait(kSilenceTimeout));
	CHECK(counter->Count() == 1);

	{
		ModelLock lock(model);
		CHECK(model.Sync(id).edits.size() == 3);
		model.Insert(0, "y");
		CHECK(model.Sync(id).edits.size() == 1);
	}
	CHECK_FALSE(counter->Wait(kSilenceTimeout));

	{
		ModelLock lock(model);
		model.Insert(0, "z");
	}
	REQUIRE(counter->Wait(kDeliveryTimeout));
	CHECK(counter->Count() == 2);
}


TEST_CASE("notifications are sent after the outermost unlock")
{
	TextModel model;
	CounterHolder counter;
	model.Lock();
	model.AddObserver(counter.Messenger());
	model.Lock();
	model.Insert(0, "abc");
	model.Unlock();
	CHECK_FALSE(counter->Wait(kSilenceTimeout));
	model.Unlock();
	CHECK(counter->Wait(kDeliveryTimeout));
}


TEST_CASE("dead observers are removed")
{
	TextModel model;
	CounterHolder counter;
	{
		ModelLock lock(model);
		model.AddObserver(counter.Messenger());
	}
	counter.Quit();
	{
		ModelLock lock(model);
		model.Insert(0, "abc");
	}
	ModelLock lock(model);
	CHECK(model.CountObservers() == 0);
}


TEST_CASE("observer falling behind the log gets a reset")
{
	TextModel model;
	CounterHolder counter;
	ModelLock lock(model);
	const ObserverId id = model.AddObserver(counter.Messenger());
	// More than the log holds.
	for (int i = 0; i < 300000; i++) {
		model.Insert(0, "x");
	}
	const ModelChanges changes = model.Sync(id);
	CHECK(changes.reset);
	CHECK(changes.edits.empty());

	model.Insert(0, "y");
	const ModelChanges next = model.Sync(id);
	CHECK_FALSE(next.reset);
	CHECK(next.edits.size() == 1);
}


TEST_CASE("invalid ranges throw without changing the model")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("abc");
	const uint64_t revision = model.Revision();
	CHECK_THROWS_AS(model.Replace(2, 2, "x"), std::out_of_range);
	CHECK_THROWS_AS(model.Insert(4, "x"), std::out_of_range);
	CHECK(model.Revision() == revision);
	CHECK(model.Text().ToString() == "abc");
	CHECK_FALSE(model.CanUndo());
}


TEST_CASE("undo and redo restore text and owner selection")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("hello");
	{
		UndoScript script(model, "Greet", 7, {0, 5});
		model.Replace(0, 5, "bye");
		model.Insert(3, "!");
		script.SetAfter({4, 4});
	}

	std::optional<UndoResult> result = model.Undo();
	REQUIRE(result.has_value());
	CHECK(result->owner == 7);
	CHECK(result->selection == Selection {0, 5});
	CHECK(model.Text().ToString() == "hello");
	CHECK_FALSE(model.CanUndo());

	result = model.Redo();
	REQUIRE(result.has_value());
	CHECK(result->selection == Selection {4, 4});
	CHECK(model.Text().ToString() == "bye!");

	model.Undo();
	model.Insert(0, "x");
	CHECK_FALSE(model.CanRedo());
	CHECK_FALSE(model.Redo().has_value());
}


TEST_CASE("undo scripts are named undo steps")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("a b");
	CHECK(model.UndoName().empty());
	{
		UndoScript outer(model, "Outer", 3, {0, 0});
		model.Replace(0, 1, "xy");
		{
			UndoScript inner(model, "Inner", 4, {1, 1});
			model.Insert(model.Text().Length(), "!");
		}
	}
	{
		UndoScript empty(model, "Empty", 3, {0, 0});
	}
	CHECK(model.Text().ToString() == "xy b!");
	CHECK(model.UndoName() == "Outer");
	CHECK(model.RedoName().empty());

	const std::optional<UndoResult> result = model.Undo();
	REQUIRE(result.has_value());
	CHECK(result->owner == 3);
	CHECK(model.Text().ToString() == "a b");
	CHECK_FALSE(model.CanUndo());
	CHECK(model.UndoName().empty());
	CHECK(model.RedoName() == "Outer");

	model.Insert(0, "z");
	CHECK(model.UndoName().empty());
	CHECK(model.RedoName().empty());
}


TEST_CASE("the selection after an undo script follows the edits")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("one two");
	{
		UndoScript script(model, "Edit", 1, {4, 7});
		model.Insert(0, ">> ");
		CHECK(script.After() == Selection {7, 10});
		// Inserted at the end of the selection: stays outside of it.
		model.Insert(10, "s");
	}
	model.Undo();
	const std::optional<UndoResult> result = model.Redo();
	REQUIRE(result.has_value());
	CHECK(result->selection == Selection {7, 10});
}


TEST_CASE("changing the line ending or BOM is an unsaved change")
{
	TextModel model;
	ModelLock lock(model);
	model.SetText("a\n");
	model.SetLineEnding(LineEnding::LF);
	CHECK_FALSE(model.IsModified());
	model.SetLineEnding(LineEnding::CRLF);
	CHECK(model.IsModified());
	model.SetSavePoint();
	CHECK_FALSE(model.IsModified());
	model.SetHasBom(true);
	CHECK(model.IsModified());
	model.SetText("b\n");
	CHECK_FALSE(model.IsModified());
}


TEST_CASE("typing and backspace are coalesced")
{
	TextModel model;
	Type(model, 1, 0, "a");
	Type(model, 1, 1, "b");
	Type(model, 1, 2, "c");
	{
		ModelLock lock(model);
		model.BreakUndoCoalescing();
	}
	Type(model, 1, 3, "d");
	Type(model, 2, 4, "e");
	Type(model, 2, 0, "f");
	CHECK(Text(model) == "fabcde");

	ModelLock lock(model);
	model.Undo();
	CHECK(model.Text().ToString() == "abcde");
	model.Undo();
	CHECK(model.Text().ToString() == "abcd");
	model.Undo();
	CHECK(model.Text().ToString() == "abc");
	const std::optional<UndoResult> result = model.Undo();
	CHECK(model.Text().ToString() == "");
	CHECK(result->selection == Selection {0, 0});

	model.SetText("abcdef");
	Backspace(model, 1, 6);
	Backspace(model, 1, 5);
	Backspace(model, 1, 4);
	CHECK(model.Text().ToString() == "abc");
	const std::optional<UndoResult> backspaceResult = model.Undo();
	CHECK(model.Text().ToString() == "abcdef");
	CHECK(backspaceResult->selection == Selection {6, 6});
	CHECK_FALSE(model.CanUndo());
}


TEST_CASE("save point tracks modification")
{
	TextModel model;
	{
		ModelLock lock(model);
		model.SetText("text");
		CHECK_FALSE(model.IsModified());
	}
	Type(model, 1, 4, "a");
	{
		ModelLock lock(model);
		CHECK(model.IsModified());
		model.SetSavePoint();
		CHECK_FALSE(model.IsModified());
	}
	Type(model, 1, 5, "b");

	ModelLock lock(model);
	CHECK(model.IsModified());
	model.Undo();
	CHECK_FALSE(model.IsModified());
	CHECK(model.Text().ToString() == "texta");
	model.Undo();
	CHECK(model.IsModified());
	model.Redo();
	CHECK_FALSE(model.IsModified());
}


TEST_CASE("marks follow their lines")
{
	TextModel model;
	MarkId mark2;
	MarkId mark3;
	auto reset = [&](MarkDeletePolicy policy) {
		ModelLock lock(model);
		model.SetText("l0\nl1\nl2\nl3\nl4\n");
		model.SharedMarks().RemoveMarks(kMarkBookmark);
		mark2 = model.SharedMarks().AddMark(kMarkBookmark, 2, 0, policy);
		mark3 = model.SharedMarks().AddMark(kMarkBookmark, 3, 0, policy);
	};
	auto lineStart = [&](size_t line) {
		ModelLock lock(model);
		return model.Text().LineStart(line);
	};
	auto edit = [&](size_t offset, size_t length, std::string_view text) {
		ModelLock lock(model);
		model.Replace(offset, length, text);
	};

	SECTION("lines inserted above") {
		reset(MarkDeletePolicy::Remove);
		edit(0, 0, "x\ny\n");
		CHECK(MarkLine(model, mark2) == 4);
		CHECK(MarkLine(model, mark3) == 5);
	}
	SECTION("newline at line start moves the mark down") {
		reset(MarkDeletePolicy::Remove);
		edit(lineStart(2), 0, "\n");
		CHECK(MarkLine(model, mark2) == 3);
	}
	SECTION("typing at line start keeps the mark") {
		reset(MarkDeletePolicy::Remove);
		edit(lineStart(2), 0, "zz");
		CHECK(MarkLine(model, mark2) == 2);
	}
	SECTION("deleting a whole line removes only its mark") {
		reset(MarkDeletePolicy::Remove);
		edit(lineStart(2), lineStart(3) - lineStart(2), "");
		CHECK_FALSE(HasMark(model, mark2));
		CHECK(MarkLine(model, mark3) == 2);
	}
	SECTION("joining lines removes the mark of the joined line") {
		reset(MarkDeletePolicy::Remove);
		edit(lineStart(3) - 1, 1, "");
		CHECK(MarkLine(model, mark2) == 2);
		CHECK_FALSE(HasMark(model, mark3));
	}
	SECTION("joining lines moves the mark with MoveToEditLine") {
		reset(MarkDeletePolicy::MoveToEditLine);
		edit(lineStart(3) - 1, 1, "");
		CHECK(MarkLine(model, mark3) == 2);
	}
	SECTION("joining into an empty line keeps the mark") {
		reset(MarkDeletePolicy::Remove);
		edit(lineStart(2), 2, "");
		edit(lineStart(3) - 1, 1, "");
		CHECK(MarkLine(model, mark3) == 2);
	}
	SECTION("undo restores positions") {
		reset(MarkDeletePolicy::Remove);
		edit(0, 0, "x\n");
		{
			ModelLock lock(model);
			model.Undo();
		}
		CHECK(MarkLine(model, mark2) == 2);
	}
	SECTION("SetText keeps line numbers clamped") {
		reset(MarkDeletePolicy::Remove);
		{
			ModelLock lock(model);
			model.SetText("a\nb\nc");
		}
		CHECK(MarkLine(model, mark2) == 2);
		CHECK(MarkLine(model, mark3) == 2);
	}
	SECTION("queries and removal") {
		reset(MarkDeletePolicy::Remove);
		ModelLock lock(model);
		const MarkId exec = model.SharedMarks().AddMark(kMarkExecLine, 1);
		CHECK(model.SharedMarks().Marks(0, 10).size() == 3);
		CHECK(model.SharedMarks().Marks(2, 2).size() == 1);
		CHECK(model.SharedMarks().Marks(1, 2).front().id == exec);
		model.SharedMarks().RemoveMarks(kMarkExecLine);
		CHECK(model.SharedMarks().Marks(0, 10).size() == 2);
		CHECK(model.SharedMarks().MoveMark(mark2, 0));
		CHECK(model.SharedMarks().FindMark(mark2)->line == 0);
		CHECK(model.SharedMarks().RemoveMark(mark2));
		CHECK_FALSE(model.SharedMarks().RemoveMark(mark2));
		CHECK(model.SharedMarks().SetMarkKind(mark3, kMarkBreakpointDisabled));
		CHECK(model.SharedMarks().FindMark(mark3)->kind == kMarkBreakpointDisabled);
		CHECK_FALSE(model.SharedMarks().SetMarkKind(mark2, kMarkBreakpoint));
	}
}


TEST_CASE("mark changes are reported to observers")
{
	TextModel model;
	CounterHolder counter;
	ObserverId id;
	{
		ModelLock lock(model);
		model.SetText("a\nb\n");
		id = model.AddObserver(counter.Messenger());
		model.SharedMarks().AddMark(kMarkBreakpoint, 1);
	}
	REQUIRE(counter->Wait(kDeliveryTimeout));
	ModelLock lock(model);
	const ModelChanges changes = model.Sync(id);
	CHECK(changes.overlaysChanged);
	CHECK(changes.edits.empty());
}


TEST_CASE("mark sets are independent and follow edits")
{
	TextModel model;
	BReference<MarkSet> session(new MarkSet(model), true);
	ModelLock lock(model);
	model.SetText("l0\nl1\nl2\nl3\n");
	const ObserverId observer = model.AddObserver();
	MarkSet &shared = model.SharedMarks();
	const MarkId bookmark = shared.AddMark(kMarkBookmark, 2);
	const MarkId exec = session->AddMark(kMarkExecLine, 2);
	const MarkId removed = session->AddMark(kMarkExecLine, 1);
	CHECK(&session->Model() == &model);
	CHECK(shared.Marks(0, 3).size() == 1);
	CHECK(session->Marks(0, 3).size() == 2);

	// Ids are per set.
	CHECK(exec == bookmark);
	CHECK(session->RemoveMark(removed));
	CHECK(shared.Marks(0, 3).size() == 1);

	model.Sync(observer);
	const uint64_t sharedRevision = shared.Revision();
	const uint64_t sessionRevision = session->Revision();
	model.Insert(0, "x\n");
	CHECK(shared.FindMark(bookmark)->line == 3);
	CHECK(session->FindMark(exec)->line == 3);
	// Moving with the text is not a change of the set.
	CHECK(shared.Revision() == sharedRevision);
	CHECK(session->Revision() == sessionRevision);
	CHECK_FALSE(model.Sync(observer).overlaysChanged);

	session->Clear();
	CHECK(session->Revision() != sessionRevision);
	CHECK(shared.Revision() == sharedRevision);
	CHECK(model.Sync(observer).overlaysChanged);

	// Deleting the line removes the marks of every set.
	session->AddMark(kMarkExecLine, 3);
	model.Erase(model.Text().LineStart(3), 3);
	CHECK(shared.Marks(0, 4).empty());
	CHECK(session->Marks(0, 4).empty());

	session->AddMark(kMarkExecLine, 1);
	model.SetText("a\nb\nc");
	CHECK(session->Marks(1, 1).size() == 1);

	model.Sync(observer);
	session.Unset();
	CHECK(model.Sync(observer).overlaysChanged);
}


TEST_CASE("decoration sets follow edits and text resets")
{
	TextModel model;
	BReference<DecorationSet> set(new DecorationSet(model), true);
	ModelLock lock(model);
	// Lines: 0 "aa", 1 "bbbb", 2 "cc".
	model.SetText("aa\nbbbb\ncc");
	const ObserverId observer = model.AddObserver();
	const rgb_color color {1, 2, 3, 100};
	set->SetDecorations({
		{DecorationKind::RangeBackground, 5, 7, color},
		{DecorationKind::LineBackground, 0, 100, color},
		{DecorationKind::GapLine, 8, 0, color},
	});
	CHECK(model.Sync(observer).overlaysChanged);
	const uint64_t revision = set->Revision();

	// Sorted and clamped.
	std::vector<Decoration> decorations = set->Decorations();
	REQUIRE(decorations.size() == 3);
	CHECK(decorations[0].kind == DecorationKind::LineBackground);
	CHECK(decorations[0].end == 10);
	CHECK(decorations[2].end == 8);

	model.Insert(0, "x\n");
	decorations = set->Decorations();
	CHECK(decorations[1].start == 7);
	CHECK(decorations[1].end == 9);
	CHECK(decorations[2].start == 10);
	CHECK(set->Revision() == revision);
	CHECK_FALSE(model.Sync(observer).overlaysChanged);

	// Lines and columns stay, clamped to the new lines.
	model.SetText("x\naa\nbbb\ncc");
	decorations = set->Decorations();
	CHECK(decorations[1].start == 7);
	CHECK(decorations[1].end == 8);
	CHECK(decorations[2].start == 9);

	set->SetDecorations(set->Decorations());
	CHECK(set->Revision() == revision);
	set->SetDecorations({});
	CHECK(set->Revision() != revision);
}


static std::string SaveToString(TextModel &model)
{
	BMallocIO stream;
	model.Save(stream);
	return std::string(static_cast<const char*>(stream.Buffer()), stream.BufferLength());
}


static void LoadFromString(TextModel &model, std::string_view data)
{
	BMallocIO stream;
	stream.WriteExactly(data.data(), data.size());
	stream.Seek(0, SEEK_SET);
	model.Load(stream);
}


TEST_CASE("load and save restore line endings and BOM")
{
	TextModel model;
	ModelLock lock(model);

	LoadFromString(model, "a\r\nb\r\n");
	CHECK(model.Text().ToString() == "a\nb\n");
	CHECK(model.GetLineEnding() == LineEnding::CRLF);
	CHECK_FALSE(model.HasBom());
	CHECK(SaveToString(model) == "a\r\nb\r\n");

	LoadFromString(model, "\xef\xbb\xbfx\ny");
	CHECK(model.Text().ToString() == "x\ny");
	CHECK(model.GetLineEnding() == LineEnding::LF);
	CHECK(model.HasBom());
	CHECK(SaveToString(model) == "\xef\xbb\xbfx\ny");

	LoadFromString(model, "a\rb\r");
	CHECK(model.Text().ToString() == "a\nb\n");
	CHECK(model.GetLineEnding() == LineEnding::CR);

	LoadFromString(model, "a\r\nb\nc\r\n");
	CHECK(model.Text().ToString() == "a\nb\nc\n");
	CHECK(model.GetLineEnding() == LineEnding::CRLF);
	CHECK(SaveToString(model) == "a\r\nb\r\nc\r\n");
	CHECK_FALSE(model.IsModified());
	CHECK_FALSE(model.CanUndo());

	std::string large;
	for (int i = 0; i < 50000; i++) {
		large += "line\r\n";
	}
	LoadFromString(model, large);
	CHECK(model.Text().LineCount() == 50001);
	CHECK(SaveToString(model) == large);
}


static std::string LoadInPieces(std::string_view data, size_t pieceSize, TextLoader &loader)
{
	for (size_t pos = 0; pos < data.size(); pos += pieceSize) {
		loader.Append(data.substr(pos, pieceSize));
	}
	loader.Finish();
	const Rope text = loader.TakeText();
	CHECK(text.Validate());
	return text.ToString();
}


TEST_CASE("text loader gives the same text however the data is split")
{
	const std::string samples[] = {
		"",
		"\xef\xbb",
		"\xef\xbb\xbf",
		"\xef\xbb\xbf" "a\r\nb",
		"a\r\nb\r\nc\r",
		"\r\r\n\n\r",
		"mixed\r\nlines\nhere\r",
		"\xe3\x81\x82\xe3\x81\x84\r\n\xe3\x81\x86",
	};
	for (const std::string &sample : samples) {
		TextLoader whole;
		const std::string expected = LoadInPieces(sample, std::max<size_t>(sample.size(), 1), whole);
		for (size_t pieceSize = 1; pieceSize <= 4; pieceSize++) {
			TextLoader split;
			CHECK(LoadInPieces(sample, pieceSize, split) == expected);
			CHECK(split.HasBom() == whole.HasBom());
			CHECK(split.DominantLineEnding() == whole.DominantLineEnding());
			CHECK(split.HasMixedLineEndings() == whole.HasMixedLineEndings());
		}
	}

	TextLoader loader;
	CHECK(LoadInPieces("\xef\xbb\xbf" "a\r\nb\r\n", 2, loader) == "a\nb\n");
	CHECK(loader.HasBom());
	CHECK(loader.DominantLineEnding() == LineEnding::CRLF);
	CHECK_FALSE(loader.HasMixedLineEndings());

	TextLoader mixed;
	CHECK(LoadInPieces("a\r\nb\nc\r", 3, mixed) == "a\nb\nc\n");
	CHECK(mixed.HasMixedLineEndings());

	TextLoader large;
	std::string data;
	for (int i = 0; i < 20000; i++) {
		data += "\xe3\x81\x82line\r\n";
	}
	std::string expected = data;
	std::erase(expected, '\r');
	CHECK(LoadInPieces(data, 4093, large) == expected);
}


TEST_CASE("rope builder makes a valid rope")
{
	for (const size_t size : {0, 1, 1023, 1024, 2047, 2048, 5000, 100000}) {
		std::string text;
		for (size_t i = 0; i < size; i++) {
			text += i % 37 == 0 ? '\n' : static_cast<char>('a' + i % 26);
		}
		Rope::Builder builder;
		for (size_t pos = 0; pos < text.size(); pos += 700) {
			builder.Append(std::string_view(text).substr(pos, 700));
		}
		const Rope rope = builder.Build();
		CHECK(rope.Validate());
		CHECK(rope.ToString() == text);
		CHECK(rope.NewlineCount() == static_cast<size_t>(std::count(text.begin(), text.end(), '\n')));
	}
}


TEST_CASE("snapshots share the text and load into other models")
{
	TextModel model;
	TextSnapshot snapshot;
	{
		ModelLock lock(model);
		LoadFromString(model, "\xef\xbb\xbfone\r\ntwo\r\n");
		model.Insert(0, "zero\n");
		snapshot = model.Snapshot();
		model.Insert(0, "later\n");
	}
	std::string written;
	snapshot.Write([&written](std::string_view data) {
		written.append(data);
	});
	CHECK(written == "\xef\xbb\xbfzero\r\none\r\ntwo\r\n");

	TextModel other;
	ModelLock lock(other);
	other.Load(snapshot);
	CHECK(other.Text().ToString() == "zero\none\ntwo\n");
	CHECK(other.GetLineEnding() == LineEnding::CRLF);
	CHECK(other.HasBom());
	CHECK_FALSE(other.CanUndo());
}


TEST_CASE("concurrent edits and snapshots")
{
	TextModel model;
	constexpr int kEditsPerThread = 2000;
	std::atomic<bool> done {};

	auto writer = [&model](char ch) {
		for (int i = 0; i < kEditsPerThread; i++) {
			ModelLock lock(model);
			model.Insert(model.Text().Length(), std::string_view(&ch, 1));
		}
	};
	std::atomic<int32_t> invalidSnapshots {};
	auto reader = [&]() {
		while (!done) {
			Rope snapshot;
			{
				ModelLock lock(model);
				snapshot = model.Text();
			}
			if (!snapshot.Validate()) {
				invalidSnapshots++;
			}
		}
	};

	std::thread readerThread(reader);
	std::thread writer1(writer, 'a');
	std::thread writer2(writer, 'b');
	writer1.join();
	writer2.join();
	done = true;
	readerThread.join();

	CHECK(invalidSnapshots == 0);
	ModelLock lock(model);
	CHECK(model.Text().Length() == 2 * kEditsPerThread);
	CHECK(model.Text().Validate());
}


} // CodeEdit
