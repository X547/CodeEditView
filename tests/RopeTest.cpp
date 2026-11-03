#include <stdint.h>

#include <algorithm>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <CodeEditView/Rope.h>


namespace CodeEdit {


static size_t RefLineStart(const std::string &text, size_t line)
{
	size_t offset = 0;
	for (size_t i = 0; i < line; i++) {
		offset = text.find('\n', offset) + 1;
	}
	return offset;
}


static size_t RefLineOfOffset(const std::string &text, size_t offset)
{
	return std::count(text.begin(), text.begin() + offset, '\n');
}


static std::string RandomText(std::mt19937 &rng, size_t minLength)
{
	static const char *const kPieces[] = {
		"a", "b", "z", " ", "\t", "\n", "{", "}",
		"\xc3\xa9",         // U+00E9
		"\xe3\x81\x82",     // U+3042
		"\xf0\x9f\x98\x80", // U+1F600
	};
	std::uniform_int_distribution<size_t> pick(0, std::size(kPieces) - 1);
	std::string text;
	while (text.size() < minLength) {
		text += kPieces[pick(rng)];
	}
	return text;
}


static void CheckEqual(const Rope &rope, const std::string &ref)
{
	REQUIRE(rope.Validate());
	REQUIRE(rope.Length() == ref.size());
	REQUIRE(rope.NewlineCount() == static_cast<size_t>(std::count(ref.begin(), ref.end(), '\n')));
	REQUIRE(rope.ToString() == ref);
}


TEST_CASE("empty rope")
{
	const Rope rope;
	CHECK(rope.Validate());
	CHECK(rope.IsEmpty());
	CHECK(rope.LineCount() == 1);
	CHECK(rope.LineStart(0) == 0);
	CHECK(rope.LineEnd(0) == 0);
	CHECK(rope.LineOfOffset(0) == 0);
	CHECK(rope.ChunkAt(0).empty());
	CHECK(rope.ToString().empty());
}


TEST_CASE("line queries")
{
	const Rope rope("ab\n\ncd\nef");
	CHECK(rope.LineCount() == 4);
	CHECK(rope.LineStart(0) == 0);
	CHECK(rope.LineStart(1) == 3);
	CHECK(rope.LineStart(2) == 4);
	CHECK(rope.LineStart(3) == 7);
	CHECK(rope.LineEnd(0) == 2);
	CHECK(rope.LineEnd(1) == 3);
	CHECK(rope.LineEnd(3) == 9);
	CHECK(rope.LineOfOffset(0) == 0);
	CHECK(rope.LineOfOffset(2) == 0);
	CHECK(rope.LineOfOffset(3) == 1);
	CHECK(rope.LineOfOffset(9) == 3);
	CHECK_THROWS_AS(rope.LineStart(4), std::out_of_range);
	CHECK_THROWS_AS(rope.LineEnd(4), std::out_of_range);
}


TEST_CASE("basic edits")
{
	Rope rope("hello world");
	rope.Insert(5, ",");
	CHECK(rope.ToString() == "hello, world");
	rope.Erase(0, 7);
	CHECK(rope.ToString() == "world");
	rope.Replace(0, 1, "W");
	CHECK(rope.ToString() == "World");
	rope.Insert(rope.Length(), "!\n");
	CHECK(rope.ToString() == "World!\n");
	CHECK(rope.ByteAt(5) == '!');
	CHECK(rope.Substring(1, 3) == "orl");
	rope.Erase(0, rope.Length());
	CHECK(rope.IsEmpty());
	CHECK(rope.Validate());
}


TEST_CASE("range errors")
{
	Rope rope("abc");
	CHECK_THROWS_AS(rope.Insert(4, "x"), std::out_of_range);
	CHECK_THROWS_AS(rope.Erase(2, 2), std::out_of_range);
	CHECK_THROWS_AS(rope.Erase(4, 0), std::out_of_range);
	CHECK_THROWS_AS(rope.ByteAt(3), std::out_of_range);
	CHECK_THROWS_AS(rope.Substring(1, 3), std::out_of_range);
	CHECK_THROWS_AS(rope.ChunkAt(4), std::out_of_range);
	CHECK_THROWS_AS(rope.LineOfOffset(4), std::out_of_range);
	CHECK(rope.ToString() == "abc");
}


TEST_CASE("large text builds a multi-level tree")
{
	std::mt19937 rng(1);
	const std::string text = RandomText(rng, 300000);
	const Rope rope(text);
	CheckEqual(rope, text);
	CHECK(rope.Height() >= 2);

	for (size_t line = 0; line < rope.LineCount(); line += 97) {
		CHECK(rope.LineStart(line) == RefLineStart(text, line));
	}
}


TEST_CASE("chunks keep UTF-8 sequences intact")
{
	std::string text;
	for (int i = 0; i < 20000; i++) {
		text += "\xe3\x81\x82";
	}
	const Rope rope(text);
	size_t pos = 0;
	while (pos < rope.Length()) {
		const std::string_view chunk = rope.ChunkAt(pos);
		REQUIRE(!chunk.empty());
		CHECK((static_cast<uint8_t>(chunk.front()) & 0xc0) != 0x80);
		CHECK(chunk.size() % 3 == 0);
		pos += chunk.size();
	}
	CHECK(pos == text.size());
}


TEST_CASE("copies are independent snapshots")
{
	Rope rope("line 1\nline 2\n");
	const Rope snapshot = rope;
	rope.Insert(0, "new\n");
	rope.Erase(rope.Length() - 3, 3);
	CHECK(snapshot.ToString() == "line 1\nline 2\n");
	CHECK(rope.ToString() == "new\nline 1\nline");
	CHECK(snapshot.Validate());
}


TEST_CASE("typing one byte at a time")
{
	Rope rope;
	std::string ref;
	std::mt19937 rng(2);
	size_t caret = 0;
	for (int i = 0; i < 30000; i++) {
		const char ch = (i % 40 == 39) ? '\n' : static_cast<char>('a' + i % 26);
		rope.Insert(caret, std::string_view(&ch, 1));
		ref.insert(caret, 1, ch);
		caret++;
		if (i % 1000 == 999) {
			caret = std::uniform_int_distribution<size_t>(0, ref.size())(rng);
		}
	}
	CheckEqual(rope, ref);
	CHECK(rope.Height() <= 4);
}


TEST_CASE("randomized edits match std::string")
{
	std::mt19937 rng(3);
	std::string ref = RandomText(rng, 50000);
	Rope rope(ref);
	std::vector<std::pair<Rope, std::string>> snapshots;

	auto randomSize = [&rng](size_t smallMax, size_t largeMax) {
		const bool large = std::uniform_int_distribution<int>(0, 9)(rng) == 0;
		return std::uniform_int_distribution<size_t>(0, large ? largeMax : smallMax)(rng);
	};

	for (int step = 0; step < 4000; step++) {
		const size_t offset = std::uniform_int_distribution<size_t>(0, ref.size())(rng);
		switch (std::uniform_int_distribution<int>(0, 2)(rng)) {
			case 0: {
				const std::string text = RandomText(rng, randomSize(8, 6000));
				rope.Insert(offset, text);
				ref.insert(offset, text);
				break;
			}
			case 1: {
				const size_t length = std::min(randomSize(16, 8000), ref.size() - offset);
				rope.Erase(offset, length);
				ref.erase(offset, length);
				break;
			}
			case 2: {
				const size_t length = std::min(randomSize(16, 3000), ref.size() - offset);
				const std::string text = RandomText(rng, randomSize(8, 3000));
				rope.Replace(offset, length, text);
				ref.replace(offset, length, text);
				break;
			}
			default:
				break;
		}

		REQUIRE(rope.Validate());
		REQUIRE(rope.Length() == ref.size());

		if (step % 50 == 0) {
			CheckEqual(rope, ref);
			for (int i = 0; i < 20; i++) {
				const size_t pos = std::uniform_int_distribution<size_t>(0, ref.size())(rng);
				REQUIRE(rope.LineOfOffset(pos) == RefLineOfOffset(ref, pos));
				const size_t line = std::uniform_int_distribution<size_t>(0, rope.NewlineCount())(rng);
				REQUIRE(rope.LineStart(line) == RefLineStart(ref, line));
				if (pos < ref.size()) {
					REQUIRE(rope.ByteAt(pos) == ref[pos]);
				}
			}
		}
		if (step % 400 == 0) {
			snapshots.emplace_back(rope, ref);
		}
	}

	CheckEqual(rope, ref);
	for (const auto &[snapshot, expected] : snapshots) {
		CheckEqual(snapshot, expected);
	}
}


} // CodeEdit
