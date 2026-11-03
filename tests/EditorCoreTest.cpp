#include <stdint.h>

#include <string>
#include <string_view>

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


} // CodeEdit
