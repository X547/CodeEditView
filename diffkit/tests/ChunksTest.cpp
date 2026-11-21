#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/TextModel.h>

#include "Chunks.h"
#include "Git.h"


namespace DiffKit {


using CodeEdit::ModelLock;
using CodeEdit::TextModel;


GitLibrary sGitLibrary;


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


static std::vector<size_t> All(const std::vector<Chunk> &chunks)
{
	std::vector<size_t> indexes;
	for (size_t i = 0; i < chunks.size(); i++) {
		indexes.push_back(i);
	}
	return indexes;
}


TEST_CASE("chunks of two texts")
{
	using Chunks = std::vector<Chunk>;
	CHECK(ComputeChunks("a\nb\n", "a\nb\n").empty());
	CHECK(ComputeChunks("", "").empty());
	CHECK(ComputeChunks("a\nb\nc\n", "a\nx\nc\n") == Chunks {{1, 1, 1, 1}});
	CHECK(ComputeChunks("a\nc\n", "a\nb\nc\n") == Chunks {{1, 0, 1, 1}});
	CHECK(ComputeChunks("a\nb\nc\n", "a\nc\n") == Chunks {{1, 1, 1, 0}});
	CHECK(ComputeChunks("b\n", "a\nb\n") == Chunks {{0, 0, 0, 1}});
	CHECK(ComputeChunks("a\n", "a\nb\n") == Chunks {{1, 0, 1, 1}});
	CHECK(ComputeChunks("", "x\n") == Chunks {{0, 0, 0, 1}});
	CHECK(ComputeChunks("x\n", "") == Chunks {{0, 1, 0, 0}});
	// A missing final newline changes the last line.
	CHECK(ComputeChunks("a\nb", "a\nb\n") == Chunks {{1, 1, 1, 1}});
	CHECK(ComputeChunks("1\n2\n3\n4\n5\n", "1\nx\n3\n4\ny\nz\n") == Chunks {{1, 1, 1, 1}, {4, 1, 4, 2}});
}


TEST_CASE("line mapping")
{
	// Line 1 of A became lines 1 to 3 of B.
	const std::vector<Chunk> chunks {{1, 1, 1, 3}};
	CHECK(MapLine(chunks, 0, true) == 0);
	CHECK(MapLine(chunks, 1, true) == 1);
	CHECK(MapLine(chunks, 1.5, true) == 2.5);
	CHECK(MapLine(chunks, 2, true) == 4);
	CHECK(MapLine(chunks, 4, false) == 2);
	CHECK(MapLine(chunks, 0.5, false) == 0.5);
	CHECK(MapLine(chunks, 2.5, false) == 1.5);

	// Insertion: the A position stays at the gap while B passes the lines.
	const std::vector<Chunk> insertion {{1, 0, 1, 2}};
	CHECK(MapLine(insertion, 2, false) == 1);
	CHECK(MapLine(insertion, 3, false) == 1);
	CHECK(MapLine(insertion, 1, true) == 3);
}


TEST_CASE("chunks touched by a line range")
{
	const std::vector<Chunk> chunks {{1, 0, 1, 2}, {5, 2, 6, 1}};
	CHECK(TouchedChunks(chunks, {2, 2}, false) == std::vector<size_t> {0});
	CHECK(TouchedChunks(chunks, {0, 0}, false).empty());
	CHECK(TouchedChunks(chunks, {0, 6}, false) == std::vector<size_t> {0, 1});
	// A side without lines is touched by the lines next to its position.
	CHECK(TouchedChunks(chunks, {0, 0}, true) == std::vector<size_t> {0});
	CHECK(TouchedChunks(chunks, {1, 1}, true) == std::vector<size_t> {0});
	CHECK(TouchedChunks(chunks, {2, 2}, true).empty());
	CHECK(TouchedChunks(chunks, {6, 9}, true) == std::vector<size_t> {1});
}


TEST_CASE("transferring whole chunks")
{
	BReference<TextModel> a = MakeModel("1\n2\n3\n4\n5");
	BReference<TextModel> b = MakeModel("0\n1\nx\n3\n5\n6\n");
	std::vector<Chunk> chunks = ComputeChunks(Text(*a), Text(*b));
	REQUIRE(chunks.size() >= 2);
	REQUIRE(chunks[1] == Chunk {1, 1, 2, 1});

	ApplyToA(*a, *b, chunks, {1}, "Copy change");
	CHECK(Text(*a) == "1\nx\n3\n4\n5");
	{
		ModelLock lock(*a);
		CHECK(a->UndoName() == "Copy change");
		REQUIRE(a->Undo().has_value());
	}
	CHECK(Text(*a) == "1\n2\n3\n4\n5");

	RevertB(*a, *b, chunks, All(chunks), "Copy change");
	CHECK(Text(*b) == "1\n2\n3\n4\n5");
	{
		ModelLock lock(*b);
		REQUIRE(b->Undo().has_value());
	}
	ApplyToA(*a, *b, chunks, All(chunks), "Copy change");
	CHECK(Text(*a) == Text(*b));
}


TEST_CASE("transferring selected lines")
{
	BReference<TextModel> a = MakeModel("1\n2\n3\n");
	BReference<TextModel> b = MakeModel("1\nx\ny\n3\n");
	const std::vector<Chunk> chunks = ComputeChunks(Text(*a), Text(*b));
	REQUIRE(chunks == std::vector<Chunk> {{1, 1, 1, 2}});

	// Staging line x: the chunk's A line goes, only x comes.
	const LineRange first {1, 1};
	ApplyToA(*a, *b, chunks, {0}, "Copy change", &first);
	CHECK(Text(*a) == "1\nx\n3\n");
	{
		ModelLock lock(*a);
		a->Undo();
	}

	// Reverting line y: the A line comes back, the unselected x stays.
	const LineRange second {2, 2};
	RevertB(*a, *b, chunks, {0}, "Copy change", &second);
	CHECK(Text(*b) == "1\n2\nx\n3\n");
	{
		ModelLock lock(*b);
		b->Undo();
	}

	// The other way: copying line 2 of A replaces the chunk's B lines.
	const std::vector<Chunk> swapped = SwapSides(chunks);
	CHECK(swapped == std::vector<Chunk> {{1, 2, 1, 1}});
	const LineRange third {1, 1};
	ApplyToA(*b, *a, swapped, {0}, "Copy change", &third);
	CHECK(Text(*b) == "1\n2\n3\n");
}


TEST_CASE("transfers keep missing final newlines")
{
	BReference<TextModel> a = MakeModel("1\n2");
	BReference<TextModel> b = MakeModel("1\n2\n3");
	std::vector<Chunk> chunks = ComputeChunks(Text(*a), Text(*b));
	ApplyToA(*a, *b, chunks, All(chunks), "Copy change");
	CHECK(Text(*a) == "1\n2\n3");

	a = MakeModel("1\nend");
	b = MakeModel("x\n1\nend");
	chunks = ComputeChunks(Text(*a), Text(*b));
	RevertB(*a, *b, chunks, All(chunks), "Copy change");
	CHECK(Text(*b) == "1\nend");

	// Reverting part of a last chunk: A's last line gets a newline when
	// kept B lines follow it.
	a = MakeModel("1\n2");
	b = MakeModel("1\nx\ny");
	chunks = ComputeChunks(Text(*a), Text(*b));
	const LineRange selection {1, 1};
	RevertB(*a, *b, chunks, All(chunks), "Copy change", &selection);
	CHECK(Text(*b) == "1\n2\ny");
}


TEST_CASE("randomized transfers")
{
	std::mt19937 random(12345);
	auto makeText = [&random]() {
		std::string text;
		const int lines = std::uniform_int_distribution<int>(0, 12)(random);
		for (int i = 0; i < lines; i++) {
			text += static_cast<char>('a' + std::uniform_int_distribution<int>(0, 4)(random));
			text += '\n';
		}
		if (!text.empty() && std::uniform_int_distribution<int>(0, 3)(random) == 0) {
			text.pop_back();
		}
		return text;
	};
	for (int round = 0; round < 300; round++) {
		const std::string aText = makeText();
		const std::string bText = makeText();
		const std::vector<Chunk> chunks = ComputeChunks(aText, bText);
		INFO("A: " << aText << "\nB: " << bText);

		BReference<TextModel> a = MakeModel(aText);
		BReference<TextModel> b = MakeModel(bText);
		ApplyToA(*a, *b, chunks, All(chunks), "Copy change");
		CHECK(Text(*a) == bText);

		a = MakeModel(aText);
		RevertB(*a, *b, chunks, All(chunks), "Copy change");
		CHECK(Text(*b) == aText);

		// One chunk at a time, from the last one, gives the same.
		a = MakeModel(aText);
		b = MakeModel(bText);
		for (size_t i = chunks.size(); i-- > 0;) {
			ApplyToA(*a, *b, chunks, {i}, "Copy change");
		}
		CHECK(Text(*a) == bText);

		// The other way with swapped chunks.
		const std::vector<Chunk> swapped = SwapSides(chunks);
		a = MakeModel(aText);
		b = MakeModel(bText);
		ApplyToA(*b, *a, swapped, All(swapped), "Copy change");
		CHECK(Text(*b) == aText);
		b = MakeModel(bText);
		RevertB(*b, *a, swapped, All(swapped), "Copy change");
		CHECK(Text(*a) == bText);
	}
}


} // DiffKit
