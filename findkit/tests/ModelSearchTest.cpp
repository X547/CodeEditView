#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ModelSearch.h"


namespace FindKit {


using CodeEdit::ModelLock;
using CodeEdit::UndoResult;


static constexpr ObserverId kOwner = 7;


static bool Is(const std::optional<Match> &match, size_t offset, size_t length)
{
	return match.has_value() && match->offset == offset && match->length == length;
}


static void SetText(TextModel &model, std::string_view text)
{
	ModelLock lock(model);
	model.SetText(text);
}


static std::string Text(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


static std::optional<UndoResult> Undo(TextModel &model)
{
	ModelLock lock(model);
	return model.Undo();
}


static bool CanUndo(TextModel &model)
{
	ModelLock lock(model);
	return model.CanUndo();
}


TEST_CASE("find next moves forward and backward with wrap")
{
	const std::string text = "foo bar foo";
	const Searcher searcher("foo", false, false);
	CHECK(Is(FindNext(searcher, StringSource(text), {0, 3}, false, false), 8, 3));
	CHECK(Is(FindNext(searcher, StringSource(text), {3, 0}, false, false), 8, 3));
	CHECK_FALSE(FindNext(searcher, StringSource(text), {8, 11}, false, false).has_value());
	CHECK(Is(FindNext(searcher, StringSource(text), {8, 11}, false, true), 0, 3));

	CHECK(Is(FindNext(searcher, StringSource(text), {8, 11}, true, false), 0, 3));
	CHECK_FALSE(FindNext(searcher, StringSource(text), {0, 3}, true, false).has_value());
	CHECK(Is(FindNext(searcher, StringSource(text), {0, 3}, true, true), 8, 3));
	CHECK(Is(FindNext(searcher, StringSource(text), {5, 5}, true, false), 0, 3));
}


TEST_CASE("find next advances over empty matches")
{
	const std::string text = "a\nb\n";
	const Searcher searcher("^", true, false);
	CHECK(Is(FindNext(searcher, StringSource(text), {0, 0}, false, false), 2, 0));
	CHECK(Is(FindNext(searcher, StringSource(text), {2, 2}, false, false), 4, 0));
	CHECK_FALSE(FindNext(searcher, StringSource(text), {4, 4}, false, false).has_value());
	CHECK(Is(FindNext(searcher, StringSource(text), {4, 4}, false, true), 0, 0));
	CHECK(Is(FindNext(searcher, StringSource(text), {2, 2}, true, false), 0, 0));
}


TEST_CASE("selection match covers exactly the selection")
{
	const std::string text = "foo food";
	const Searcher searcher("fo+", true, false);
	CHECK(Is(SelectionMatch(searcher, StringSource(text), {0, 3}), 0, 3));
	CHECK(Is(SelectionMatch(searcher, StringSource(text), {3, 0}), 0, 3));
	CHECK_FALSE(SelectionMatch(searcher, StringSource(text), {0, 2}).has_value());
	CHECK_FALSE(SelectionMatch(searcher, StringSource(text), {4, 8}).has_value());
	CHECK_FALSE(SelectionMatch(Searcher("x*", true, false), StringSource(text), {2, 2}).has_value());
}


TEST_CASE("replace selection is one undo step restoring the selection")
{
	TextModel model;
	SetText(model, "foo bar");
	const Searcher searcher("(o+)", true, false);

	CHECK_FALSE(ReplaceSelection(model, kOwner, searcher, "[$1]", {0, 3}).has_value());
	CHECK(Text(model) == "foo bar");
	CHECK_FALSE(CanUndo(model));

	const std::optional<Selection> after = ReplaceSelection(model, kOwner, searcher, "[$1]", {3, 1});
	REQUIRE(after.has_value());
	CHECK(*after == Selection {1, 5});
	CHECK(Text(model) == "f[oo] bar");

	const std::optional<UndoResult> undo = Undo(model);
	REQUIRE(undo.has_value());
	CHECK(undo->owner == kOwner);
	CHECK(undo->selection == Selection {3, 1});
	CHECK(Text(model) == "foo bar");
	CHECK_FALSE(CanUndo(model));
}


TEST_CASE("replace all is one undo step")
{
	TextModel model;
	SetText(model, "a1 b22 c333");
	const ReplaceAllResult result = ReplaceAll(model, kOwner, Searcher("\\d+", true, false), "#", {2, 2}, false);
	CHECK(result.count == 3);
	CHECK(Text(model) == "a# b# c#");
	CHECK(result.selection == Selection {8, 8});

	const std::optional<UndoResult> undo = Undo(model);
	REQUIRE(undo.has_value());
	CHECK(undo->owner == kOwner);
	CHECK(undo->selection == Selection {2, 2});
	CHECK(Text(model) == "a1 b22 c333");
	CHECK_FALSE(CanUndo(model));
}


TEST_CASE("replace all matches multi-line exact strings")
{
	TextModel model;
	SetText(model, "x\ny\nx\ny\n");
	const ReplaceAllResult result = ReplaceAll(model, kOwner, Searcher("x\ny", false, false), "z", {0, 0}, false);
	CHECK(result.count == 2);
	CHECK(Text(model) == "z\nz\n");
	CHECK(result.selection == Selection {3, 3});
}


TEST_CASE("replace all in selection skips matches crossing its edges")
{
	TextModel model;
	SetText(model, "ab ab ab ab");
	const ReplaceAllResult result = ReplaceAll(model, kOwner, Searcher("ab", false, false), "XYZ", {8, 1}, true);
	CHECK(result.count == 2);
	CHECK(Text(model) == "ab XYZ XYZ ab");
	CHECK(result.selection == Selection {1, 10});

	const std::optional<UndoResult> undo = Undo(model);
	REQUIRE(undo.has_value());
	CHECK(undo->selection == Selection {8, 1});
}


TEST_CASE("models are searched in place like strings")
{
	std::string text;
	for (int i = 0; i < 3000; i++) {
		text += "line " + std::to_string(i) + (i % 10 == 0 ? " match\n" : "\n");
	}
	TextModel model;
	SetText(model, text);
	ModelLock lock(model);
	const RopeSource rope(model.Text());
	const StringSource string(text);
	for (const bool regex : {false, true}) {
		const Searcher searcher(regex ? "\\d+ match$" : "match", regex, false);
		CHECK(searcher.FindAll(rope, 0, text.size()).size() == 300);
		for (size_t from = 0; from < text.size(); from += 997) {
			const std::optional<Match> a = searcher.Find(rope, from);
			const std::optional<Match> b = searcher.Find(string, from);
			CHECK(a.has_value() == b.has_value());
			if (a.has_value() && b.has_value()) {
				CHECK((a->offset == b->offset && a->length == b->length));
			}
			const std::optional<Match> c = searcher.FindLast(rope, from);
			const std::optional<Match> d = searcher.FindLast(string, from);
			CHECK(c.has_value() == d.has_value());
			if (c.has_value() && d.has_value()) {
				CHECK(c->offset == d->offset);
			}
		}
	}
}


TEST_CASE("replace all of many matches")
{
	std::string text;
	for (int i = 0; i < 20000; i++) {
		text += "a" + std::to_string(i) + "\n";
	}
	TextModel model;
	SetText(model, text);
	const ReplaceAllResult result = ReplaceAll(model, kOwner, Searcher("a(\\d+)", true, false), "b$1", {0, 0}, false);
	CHECK(result.count == 20000);
	std::string expected = text;
	std::replace(expected.begin(), expected.end(), 'a', 'b');
	CHECK(Text(model) == expected);
	REQUIRE(Undo(model).has_value());
	CHECK(Text(model) == text);
}


TEST_CASE("replace all without matches leaves the model alone")
{
	TextModel model;
	SetText(model, "abc");
	const Searcher searcher("x", false, false);
	CHECK(ReplaceAll(model, kOwner, searcher, "y", {1, 2}, false).count == 0);
	CHECK(ReplaceAll(model, kOwner, Searcher("b", false, false), "y", {1, 1}, true).count == 0);
	CHECK(Text(model) == "abc");
	CHECK_FALSE(CanUndo(model));
}


} // FindKit
