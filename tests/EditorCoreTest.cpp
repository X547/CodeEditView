#include <stdint.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "view/EditorCore.h"


namespace CodeEdit {


static BReference<TextModel> MakeModel(std::string_view text)
{
	BReference<TextModel> model(new TextModel(), true);
	ModelLock lock(*model);
	model->SetText(text);
	return model;
}


static std::string Text(TextModel &model)
{
	ModelLock lock(model);
	return model.Text().ToString();
}


static size_t Caret(EditorCore &core)
{
	return core.GetSelection().caret;
}


static void Place(EditorCore &core, size_t offset)
{
	core.SetSelection({offset, offset});
}


static size_t MoveFrom(EditorCore &core, size_t offset, Movement movement)
{
	Place(core, offset);
	core.Move(movement, false, {});
	return Caret(core);
}


TEST_CASE("character movement")
{
	EditorCore core(MakeModel("abc\ndef\néx"));
	CHECK(MoveFrom(core, 1, Movement::CharacterRight) == 2);
	CHECK(MoveFrom(core, 3, Movement::CharacterRight) == 4);
	CHECK(MoveFrom(core, 4, Movement::CharacterLeft) == 3);
	CHECK(MoveFrom(core, 0, Movement::CharacterLeft) == 0);
	CHECK(MoveFrom(core, 8, Movement::CharacterRight) == 11);
	CHECK(MoveFrom(core, 11, Movement::CharacterLeft) == 8);

	core.SetSelection({1, 3});
	core.Move(Movement::CharacterLeft, false, {});
	CHECK(core.GetSelection() == Selection {1, 1});
	core.SetSelection({3, 1});
	core.Move(Movement::CharacterRight, false, {});
	CHECK(core.GetSelection() == Selection {3, 3});

	Place(core, 1);
	core.Move(Movement::CharacterRight, true, {});
	core.Move(Movement::CharacterRight, true, {});
	CHECK(core.GetSelection() == Selection {1, 3});

	core.SetSelection({9, 9});
	CHECK(core.GetSelection() == Selection {11, 11});
}


TEST_CASE("word and sub-word movement")
{
	EditorCore core(MakeModel("foo_bar  baz.qux\nnext"));
	size_t caret = 0;
	const size_t forward[] = {7, 12, 16, 21, 21};
	for (const size_t expected : forward) {
		caret = MoveFrom(core, caret, Movement::WordRight);
		CHECK(caret == expected);
	}
	const size_t backward[] = {17, 13, 9, 0, 0};
	for (const size_t expected : backward) {
		caret = MoveFrom(core, caret, Movement::WordLeft);
		CHECK(caret == expected);
	}

	EditorCore subword(MakeModel("fooBarBaz_qux"));
	CHECK(MoveFrom(subword, 0, Movement::SubwordRight) == 3);
	CHECK(MoveFrom(subword, 3, Movement::SubwordRight) == 6);
	CHECK(MoveFrom(subword, 6, Movement::SubwordRight) == 9);
	CHECK(MoveFrom(subword, 9, Movement::SubwordRight) == 13);
	CHECK(MoveFrom(subword, 13, Movement::SubwordLeft) == 10);
	CHECK(MoveFrom(subword, 10, Movement::SubwordLeft) == 6);
	CHECK(MoveFrom(subword, 0, Movement::WordRight) == 13);

	EditorCore cjk(MakeModel("日本語のテキスト"));
	CHECK(MoveFrom(cjk, 0, Movement::WordRight) == 9);
	CHECK(MoveFrom(cjk, 9, Movement::WordRight) == 12);
	CHECK(MoveFrom(cjk, 12, Movement::WordRight) == 24);
	CHECK(MoveFrom(cjk, 24, Movement::WordLeft) == 12);
}


TEST_CASE("smart home and end")
{
	EditorCore core(MakeModel("x\n    code\n"));
	CHECK(MoveFrom(core, 10, Movement::LineStart) == 6);
	CHECK(MoveFrom(core, 6, Movement::LineStart) == 2);
	CHECK(MoveFrom(core, 2, Movement::LineStart) == 6);
	CHECK(MoveFrom(core, 4, Movement::LineStart) == 2);
	CHECK(MoveFrom(core, 2, Movement::LineEnd) == 10);

	EditorSettings settings;
	settings.smartHome = false;
	core.SetSettings(settings);
	CHECK(MoveFrom(core, 10, Movement::LineStart) == 2);
}


TEST_CASE("vertical movement keeps the goal column")
{
	EditorCore core(MakeModel("abcdef\nab\nabcdef"));
	Place(core, 5);
	core.Move(Movement::NextLine, false, {});
	CHECK(Caret(core) == 9);
	core.Move(Movement::NextLine, false, {});
	CHECK(Caret(core) == 15);
	core.Move(Movement::PreviousLine, false, {});
	core.Move(Movement::PreviousLine, false, {});
	CHECK(Caret(core) == 5);
	core.Move(Movement::PreviousLine, false, {});
	CHECK(Caret(core) == 0);
	core.Move(Movement::NextLine, false, {});
	CHECK(Caret(core) == 9);

	CHECK(MoveFrom(core, 15, Movement::NextLine) == 16);

	core.SetSelection({1, 3});
	core.Move(Movement::NextLine, false, {});
	CHECK(core.GetSelection() == Selection {3, 3});

	core.SetSelection({1, 1});
	core.Move(Movement::NextLine, true, {});
	CHECK(core.GetSelection() == Selection {1, 8});

	EditorCore wide(MakeModel("あいう\nabcdef"));
	CHECK(MoveFrom(wide, 3, Movement::NextLine) == 12);
	CHECK(MoveFrom(wide, 12, Movement::PreviousLine) == 3);
	CHECK(MoveFrom(wide, 13, Movement::PreviousLine) == 6);
}


TEST_CASE("page movement keeps context lines")
{
	std::string text;
	for (int i = 0; i < 100; i++) {
		text += "line\n";
	}
	EditorCore core(MakeModel(text));
	auto row = [&]() {
		return core.PositionOfOffset(Caret(core)).row;
	};

	const PageInfo top {0, 20};
	core.Move(Movement::PageBottom, false, top);
	CHECK(row() == 16);
	core.Move(Movement::PageBottom, false, top);
	CHECK(row() == 30);

	const PageInfo scrolled {20, 20};
	core.Move(Movement::PageTop, false, scrolled);
	CHECK(row() == 23);
	core.Move(Movement::PageTop, false, scrolled);
	CHECK(row() == 9);
	core.Move(Movement::PageTop, false, top);
	CHECK(row() == 3);
	core.Move(Movement::PageTop, false, top);
	CHECK(row() == 0);

	core.Move(Movement::FileEnd, false, top);
	CHECK(Caret(core) == text.size());
	core.Move(Movement::PageBottom, false, {90, 20});
	CHECK(row() == 100);
}


TEST_CASE("typing, deletion and undo")
{
	BReference<TextModel> model = MakeModel("ab\ncd");
	EditorCore core(model);

	Place(core, 5);
	core.InsertText("x");
	core.InsertText("y");
	core.InsertText("z");
	CHECK(Text(*model) == "ab\ncdxyz");
	CHECK(core.Undo());
	CHECK(Text(*model) == "ab\ncd");
	CHECK(core.GetSelection() == Selection {5, 5});
	CHECK(core.Redo());
	CHECK(Caret(core) == 8);

	core.SetSelection({0, 2});
	core.InsertText("Q");
	CHECK(Text(*model) == "Q\ncdxyz");

	Place(core, 2);
	core.Delete(Deletion::CharacterLeft);
	CHECK(Text(*model) == "Qcdxyz");
	CHECK(Caret(core) == 1);

	core.Delete(Deletion::CharacterRight);
	CHECK(Text(*model) == "Qdxyz");

	Place(core, 3);
	core.Delete(Deletion::ToLineStart);
	CHECK(Text(*model) == "yz");
	core.Delete(Deletion::ToLineEnd);
	CHECK(Text(*model) == "");
	core.Delete(Deletion::CharacterLeft);
	CHECK(Text(*model) == "");

	core.InsertText("é");
	core.Delete(Deletion::CharacterLeft);
	CHECK(Text(*model) == "");
}


TEST_CASE("newline, tab and paste")
{
	BReference<TextModel> model = MakeModel("    foo");
	EditorCore core(model);

	Place(core, 7);
	core.InsertNewline();
	CHECK(Text(*model) == "    foo\n    ");
	CHECK(Caret(core) == 12);

	Place(core, 2);
	core.InsertNewline();
	CHECK(Text(*model) == "  \n  " "  foo\n    ");

	EditorSettings settings;
	settings.insertSpacesForTab = true;
	core.SetSettings(settings);
	{
		ModelLock lock(*model);
		model->SetText("ab");
	}
	Place(core, 2);
	core.InsertTab();
	CHECK(Text(*model) == "ab  ");

	core.SelectAll();
	core.Paste("a\r\nb\rc");
	CHECK(Text(*model) == "a\nb\nc");
	CHECK(Caret(core) == 5);
}


TEST_CASE("hard wrap while typing")
{
	BReference<TextModel> model = MakeModel("subject line that is long\n\n  body");
	EditorCore core(model);
	EditorSettings settings;
	settings.hardWrapColumn = 10;
	settings.hardWrapFirstLine = 1;
	core.SetSettings(settings);

	// The first line is not wrapped.
	Place(core, 25);
	core.InsertText("x");
	CHECK(Text(*model) == "subject line that is longx\n\n  body");

	// Typing past the column moves the last word to a new line with the
	// same indentation; the caret follows the typed text.
	const std::string start = Text(*model);
	Place(core, start.size());
	for (const char *typed : {" ", "t", "e", "x"}) {
		core.InsertText(typed);
	}
	CHECK(Text(*model) == start + " tex");
	core.InsertText("t");
	CHECK(Text(*model) == start + "\n  text");
	CHECK(Caret(core) == Text(*model).size());

	CHECK(core.Undo());
	CHECK(Text(*model).find("\n  t") == std::string::npos);
	CHECK(core.Redo());
	CHECK(Text(*model) == start + "\n  text");

	// A word longer than the column is not broken.
	Place(core, Text(*model).size());
	for (const char *typed : {"a", "b", "c", "d", "e", "f"}) {
		core.InsertText(typed);
	}
	CHECK(Text(*model) == start + "\n  textabcdef");
}


TEST_CASE("cores sharing a model follow each other's edits")
{
	BReference<TextModel> model = MakeModel("hello world");
	EditorCore first(model);
	EditorCore second(model);

	Place(second, 6);
	Place(first, 0);
	first.InsertText("XX");
	CHECK(second.GetSelection() == Selection {8, 8});

	second.SetSelection({8, 13});
	first.InsertText("\n");
	CHECK(second.GetSelection() == Selection {9, 14});
	CHECK(second.RowCount() == 2);
	second.TakeDirty();

	second.InsertText("Z");
	CHECK(Text(*model) == "XX\nhello Z");
	CHECK(first.Undo());
	CHECK(Text(*model) == "XX\nhello world");
	CHECK(first.GetSelection() == Selection {14, 14});
	CHECK(second.Redo());
	CHECK(second.GetSelection() == Selection {10, 10});

	const DirtyRows dirty = second.TakeDirty();
	CHECK_FALSE(dirty.IsEmpty());
	CHECK(dirty.first <= 1);
}


TEST_CASE("mouse selection units")
{
	BReference<TextModel> model = MakeModel("int foo_bar = baz(1, (2));\nnext line\n");
	EditorCore core(model);

	CHECK(core.UnitRangeAt(5, SelectionUnit::Word) == TextRange {4, 11});
	CHECK(core.UnitRangeAt(4, SelectionUnit::Word) == TextRange {4, 11});
	CHECK(core.UnitRangeAt(10, SelectionUnit::Word) == TextRange {4, 11});
	CHECK(core.UnitRangeAt(11, SelectionUnit::Word) == TextRange {11, 12});
	CHECK(core.UnitRangeAt(12, SelectionUnit::Word) == TextRange {12, 13});
	CHECK(core.UnitRangeAt(26, SelectionUnit::Word) == TextRange {26, 26});
	CHECK(core.UnitRangeAt(5, SelectionUnit::Line) == TextRange {0, 27});
	CHECK(core.UnitRangeAt(30, SelectionUnit::Line) == TextRange {27, 37});
	CHECK(core.UnitRangeAt(5, SelectionUnit::Character) == TextRange {5, 5});

	// "baz(1, (2))": '(' at 17 pairs with ')' at 24.
	CHECK(core.BalancedRangeAt(17) == TextRange {18, 24});
	CHECK(core.BalancedRangeAt(24) == TextRange {18, 24});
	CHECK(core.BalancedRangeAt(21) == TextRange {22, 23});
	CHECK_FALSE(core.BalancedRangeAt(5).has_value());

	const TextRange word {4, 11};
	core.ExtendSelection(word, 15, SelectionUnit::Word);
	CHECK(core.GetSelection() == Selection {4, 17});
	core.ExtendSelection(word, 1, SelectionUnit::Word);
	CHECK(core.GetSelection() == Selection {11, 0});
	core.ExtendSelection(word, 6, SelectionUnit::Word);
	CHECK(core.GetSelection() == Selection {4, 11});

	const TextRange line {0, 27};
	core.ExtendSelection(line, 30, SelectionUnit::Line);
	CHECK(core.GetSelection() == Selection {0, 37});
	core.ExtendSelection({27, 37}, 3, SelectionUnit::Line);
	CHECK(core.GetSelection() == Selection {37, 0});

	core.ExtendSelection({8, 8}, 2, SelectionUnit::Character);
	CHECK(core.GetSelection() == Selection {8, 2});
}


TEST_CASE("hit testing cells")
{
	EditorCore core(MakeModel("aあb\nxy"));
	CHECK(core.HitTestCell(0, 0.5f).offset == 0);
	CHECK(core.HitTestCell(0, 1.2f).offset == 1);
	CHECK(core.HitTestCell(0, 2.8f).offset == 1);
	CHECK(core.HitTestCell(0, 3.1f).offset == 4);
	const CellHit past = core.HitTestCell(0, 9);
	CHECK(past.offset == 5);
	CHECK(past.pastLineEnd);
	CHECK(core.HitTestCell(5, 0).offset == 6);
}


TEST_CASE("drag and drop editing")
{
	BReference<TextModel> model = MakeModel("one two three");
	EditorCore core(model);

	core.SetSelection({4, 7});
	core.MoveText({4, 7}, 13);
	CHECK(Text(*model) == "one  threetwo");
	CHECK(core.GetSelection() == Selection {10, 13});
	CHECK(core.Undo());
	CHECK(Text(*model) == "one two three");
	CHECK(core.GetSelection() == Selection {4, 7});

	core.MoveText({4, 7}, 0);
	CHECK(Text(*model) == "twoone  three");
	CHECK(core.GetSelection() == Selection {0, 3});

	core.MoveText({0, 3}, 2);
	CHECK(Text(*model) == "twoone  three");

	core.InsertTextAt(3, "\r\nX");
	CHECK(Text(*model) == "two\nXone  three");
	CHECK(core.GetSelection() == Selection {3, 5});
	CHECK(core.Undo());
	CHECK(Text(*model) == "twoone  three");
}


TEST_CASE("indent and unindent lines")
{
	BReference<TextModel> model = MakeModel("a\n\n\tb\n  c\nd\n");
	EditorCore core(model);
	const auto lineStart = [&model](size_t line) {
		ModelLock lock(*model);
		return model->Text().LineStart(line);
	};

	// Selection from line 0 to the start of line 4: lines 0-3, empty line 1 skipped.
	core.SetSelection({0, lineStart(4)});
	core.ChangeIndent(true);
	CHECK(Text(*model) == "\ta\n\n\t\tb\n\t  c\nd\n");
	CHECK(core.GetSelection() == Selection {1, lineStart(4)});

	core.ChangeIndent(false);
	CHECK(Text(*model) == "a\n\n\tb\n  c\nd\n");
	core.ChangeIndent(false);
	CHECK(Text(*model) == "a\n\nb\nc\nd\n");
	CHECK(core.Undo());
	CHECK(Text(*model) == "a\n\n\tb\n  c\nd\n");

	// Without a selection only the caret line changes; the caret stays on its text.
	core.SetSelection({lineStart(4) + 1, lineStart(4) + 1});
	core.ChangeIndent(true);
	CHECK(Text(*model) == "a\n\n\tb\n  c\n\td\n");
	CHECK(core.GetSelection().caret == lineStart(4) + 2);

	EditorSettings settings;
	settings.insertSpacesForTab = true;
	settings.indentSize = 2;
	core.SetSettings(settings);
	core.ChangeIndent(true);
	CHECK(Text(*model) == "a\n\n\tb\n  c\n  \td\n");
	core.SetSelection({lineStart(3), lineStart(3)});
	core.ChangeIndent(false);
	CHECK(Text(*model) == "a\n\n\tb\nc\n  \td\n");
	core.ChangeIndent(false);
	CHECK(Text(*model) == "a\n\n\tb\nc\n  \td\n");
}


TEST_CASE("bracket match at the caret")
{
	// f0 (1 a2 [3 14 ]5 )6 7 {8 x9 }10
	EditorCore core(MakeModel("f(a[1]) {x}"));
	auto matchAt = [&core](size_t caret) {
		Place(core, caret);
		core.UpdateBracketMatch();
		return core.BracketMatch();
	};

	CHECK(matchAt(1) == BracketPair {1, 6});
	CHECK(matchAt(2) == BracketPair {1, 6});
	CHECK(matchAt(4) == BracketPair {3, 5});
	CHECK(matchAt(7) == BracketPair {6, 1});
	CHECK(matchAt(9) == BracketPair {8, 10});
	CHECK(matchAt(11) == BracketPair {10, 8});
	CHECK_FALSE(matchAt(0).has_value());

	core.SetSelection({1, 3});
	core.UpdateBracketMatch();
	CHECK_FALSE(core.BracketMatch().has_value());

	EditorCore unbalanced(MakeModel("(("));
	Place(unbalanced, 1);
	unbalanced.UpdateBracketMatch();
	CHECK_FALSE(unbalanced.BracketMatch().has_value());

	EditorCore far(MakeModel("{\na\nb\nc\n}"));
	far.TakeDirty();
	Place(far, 9);
	far.TakeDirty();
	far.UpdateBracketMatch();
	CHECK(far.BracketMatch() == BracketPair {8, 0});
	const DirtyRows dirty = far.TakeDirty();
	CHECK(dirty.first == 0);
	CHECK(dirty.last == 4);
}


TEST_CASE("caret location in display columns")
{
	EditorCore core(MakeModel("ab\n\txあy"));
	Place(core, 0);
	CHECK(core.CaretLocation() == TextPoint {0, 0});
	Place(core, 2);
	CHECK(core.CaretLocation() == TextPoint {0, 2});
	Place(core, 5);
	CHECK(core.CaretLocation() == TextPoint {1, 5});
	Place(core, 8);
	CHECK(core.CaretLocation() == TextPoint {1, 7});
}


TEST_CASE("dirty rows and scroll anchor")
{
	BReference<TextModel> model = MakeModel("aaaa\nb\nc\nd\ne\nf\ng");
	EditorCore core(model);
	EditorCore other(model);
	core.TakeDirty();

	Place(core, 7);
	core.TakeDirty();
	core.InsertText("x");
	DirtyRows dirty = core.TakeDirty();
	CHECK(dirty.first == 2);
	CHECK(dirty.last == 2);
	CHECK_FALSE(dirty.scrollRangeChanged);

	core.InsertNewline();
	dirty = core.TakeDirty();
	CHECK(dirty.first == 2);
	CHECK(dirty.last == SIZE_MAX);
	CHECK(dirty.scrollRangeChanged);

	core.SetScrollAnchor(core.RowStartOffset(5));
	Place(other, 0);
	other.InsertText("1\n2\n");
	core.Sync();
	CHECK(core.PositionOfOffset(core.ScrollAnchor()).row == 7);
}


TEST_CASE("read-only view")
{
	BReference<TextModel> model = MakeModel("abc\ndef");
	EditorCore core(model);
	EditorCore other(model);
	EditorSettings settings = core.Settings();
	settings.readOnly = true;
	core.SetSettings(settings);

	Place(core, 1);
	core.InsertText("x");
	core.InsertNewline();
	core.InsertTab();
	core.Delete(Deletion::CharacterLeft);
	core.Paste("y");
	core.ChangeIndent(true);
	core.SetSelection({0, 2});
	core.DeleteSelection();
	core.MoveText({0, 2}, 5);
	core.InsertTextAt(0, "z");
	CHECK(Text(*model) == "abc\ndef");

	// Navigation and selection still work.
	CHECK(core.GetSelection() == Selection {0, 2});
	Place(core, 2);
	core.Move(Movement::NextLine, false, {});
	CHECK(Caret(core) == 6);

	// Other views of the model still edit, but this one does not undo.
	Place(other, 0);
	other.InsertText("1");
	CHECK_FALSE(core.Undo());
	CHECK(Text(*model) == "1abc\ndef");
	CHECK(other.Undo());
	CHECK(Text(*model) == "abc\ndef");
}


TEST_CASE("line rows")
{
	BReference<TextModel> model = MakeModel("a\nb\nc\nd\ne\n");
	EditorCore core(model);
	CHECK(core.LineRows(0) == std::pair<size_t, size_t> {0, 1});
	CHECK(core.LineRows(5) == std::pair<size_t, size_t> {5, 1});
	CHECK(core.LineRows(6) == std::pair<size_t, size_t> {6, 0});
	CHECK(core.LineRows(100) == std::pair<size_t, size_t> {6, 0});

	core.Fold({1, 3});
	CHECK(core.LineRows(1) == std::pair<size_t, size_t> {1, 1});
	CHECK(core.LineRows(2) == std::pair<size_t, size_t> {2, 0});
	CHECK(core.LineRows(3) == std::pair<size_t, size_t> {2, 0});
	CHECK(core.LineRows(4) == std::pair<size_t, size_t> {2, 1});
}


TEST_CASE("decorations follow edits")
{
	// Lines: 0 "aa", 1 "bb", 2 "cc", 3 "dd".
	BReference<TextModel> model = MakeModel("aa\nbb\ncc\ndd");
	EditorCore core(model);
	EditorCore other(model);
	const rgb_color color {1, 2, 3, 100};
	core.SetDecorations({
		{DecorationKind::LineBackground, 3, 9, color},
		{DecorationKind::RangeBackground, 4, 5, color},
		{DecorationKind::GapLine, 9, 9, color},
		{DecorationKind::GapLine, 11, 0, color},
	});

	LineDecorations lines = core.DecorationsOfLines(0, 3);
	REQUIRE(lines.lineBackgrounds.size() == 2);
	CHECK(lines.lineBackgrounds[0].first == 1);
	CHECK(lines.lineBackgrounds[1].first == 2);
	REQUIRE(lines.ranges.size() == 1);
	REQUIRE(lines.gaps.size() == 2);
	CHECK(lines.gaps[0].line == 3);
	CHECK_FALSE(lines.gaps[0].bottom);
	// The end of a text without final newline: below the last line.
	CHECK(lines.gaps[1].line == 3);
	CHECK(lines.gaps[1].bottom);
	CHECK(core.DecorationsOfLines(0, 0).lineBackgrounds.empty());

	// Lines inserted above move them; text inserted at a range start stays
	// outside, at a gap line after it.
	Place(other, 0);
	other.InsertText("x\n");
	Place(other, 5);
	other.InsertText("y");
	Place(other, 12);
	other.InsertText("z");
	const std::vector<Decoration> decorations = core.Decorations();
	REQUIRE(decorations.size() == 4);
	CHECK(decorations[0].start == 6);
	CHECK(decorations[0].end == 12);
	CHECK(decorations[1].start == 7);
	CHECK(decorations[1].end == 8);
	CHECK(decorations[2].start == 12);
	CHECK(decorations[3].start == 15);
	lines = core.DecorationsOfLines(0, 4);
	REQUIRE(lines.lineBackgrounds.size() == 2);
	CHECK(lines.lineBackgrounds[0].first == 2);
	CHECK(lines.gaps[0].line == 4);
	CHECK_FALSE(lines.gaps[0].bottom);
}


TEST_CASE("views show the mark and decoration sets given them")
{
	// Lines: 0 "aa", 1 "bb", 2 "cc", 3 "dd".
	BReference<TextModel> model = MakeModel("aa\nbb\ncc\ndd");
	BReference<MarkSet> session(new MarkSet(*model), true);
	BReference<DecorationSet> coverage(new DecorationSet(*model), true);
	EditorCore shown(model);
	EditorCore other(model);
	shown.SetMarkSets({session});
	shown.SetDecorationSets({coverage});
	CHECK(shown.TakeDirty().marksChanged);
	other.TakeDirty();

	const rgb_color red {255, 0, 0, 100};
	const rgb_color blue {0, 0, 255, 100};
	{
		ModelLock lock(*model);
		model->SharedMarks().AddMark(kMarkBreakpoint, 2);
		session->AddMark(kMarkExecLine, 2);
		session->AddMark(kMarkExecLine, 0);
		coverage->SetDecorations({{DecorationKind::LineBackground, 3, 3, red}});
	}
	const std::vector<MarkInfo> marks = shown.Marks(0, 3);
	REQUIRE(marks.size() == 3);
	CHECK(marks[0].line == 0);
	// The shared marks first.
	CHECK(marks[1].kind == kMarkBreakpoint);
	CHECK(marks[2].kind == kMarkExecLine);
	REQUIRE(other.Marks(0, 3).size() == 1);
	CHECK(shown.TakeDirty().marksChanged);
	CHECK(other.TakeDirty().marksChanged);

	// Host decorations are drawn over the sets.
	shown.SetDecorations({{DecorationKind::LineBackground, 3, 3, blue}});
	const LineDecorations lines = shown.DecorationsOfLines(0, 3);
	REQUIRE(lines.lineBackgrounds.size() == 2);
	CHECK(lines.lineBackgrounds[0].second == red);
	CHECK(lines.lineBackgrounds[1].second == blue);
	CHECK(other.DecorationsOfLines(0, 3).lineBackgrounds.empty());

	// Changes of sets a view does not show do not repaint it.
	shown.TakeDirty();
	{
		ModelLock lock(*model);
		session->RemoveMarks(kMarkExecLine);
	}
	shown.Sync();
	other.Sync();
	CHECK(shown.TakeDirty().marksChanged);
	const DirtyRows dirty = other.TakeDirty();
	CHECK_FALSE(dirty.marksChanged);
	CHECK(dirty.IsEmpty());
	{
		ModelLock lock(*model);
		coverage->SetDecorations({});
	}
	shown.Sync();
	other.Sync();
	CHECK_FALSE(shown.TakeDirty().IsEmpty());
	CHECK(other.TakeDirty().IsEmpty());

	BReference<TextModel> otherModel = MakeModel("x");
	BReference<MarkSet> foreign(new MarkSet(*otherModel), true);
	CHECK_THROWS_AS(shown.SetMarkSets({foreign}), std::invalid_argument);
}


} // CodeEdit
