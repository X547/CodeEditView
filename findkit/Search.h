#pragma once

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


struct pcre2_real_match_data_8;


namespace FindKit {


constexpr size_t kNoGroup = SIZE_MAX;


// The pattern does not compile; what() is PCRE2's message.
class PatternError: public std::runtime_error {
private:
	size_t fOffset {};

public:
	PatternError(const std::string &message, size_t offset):
		std::runtime_error(message),
		fOffset(offset)
	{
	}

	// Byte offset in the pattern where the error was detected.
	size_t Offset() const {return fOffset;}
};


// Matching failed for another reason than no match (e.g. the regex
// backtracks too much).
class SearchError: public std::runtime_error {
public:
	using std::runtime_error::runtime_error;
};


struct Match {
	size_t offset {};
	size_t length {};
	// Regex mode: capture groups as (offset, length), [0] is the whole match;
	// an unset group has offset kNoGroup. Empty in exact mode.
	std::vector<std::pair<size_t, size_t>> groups;

	size_t End() const {return offset + length;}
};


// A compiled search pattern. Text and pattern are UTF-8; offsets are bytes.
//
// Exact mode compares bytes (with ignoreCase: Unicode case folding). Regex
// mode is PCRE2 with ^ and $ matching at line boundaries; '.' does not match
// a newline. Searches always see the whole text, so ^, \b and lookbehind at
// the start offset see the real context. Invalid UTF-8 in the text never
// matches a pattern character but does not stop the search.
class Searcher {
private:
	struct Code;

	std::string fPattern;
	bool fRegex {};
	// PCRE2 code; nullptr for case-sensitive exact search, which is a plain
	// byte search.
	std::unique_ptr<Code> fCode;

	std::optional<Match> Run(std::string_view text, size_t from, bool anchored, pcre2_real_match_data_8 *matchData) const;
	std::optional<Match> FindWith(std::string_view text, size_t from, pcre2_real_match_data_8 *matchData) const;

public:
	// Throws PatternError for an empty or invalid pattern.
	Searcher(std::string_view pattern, bool regex, bool ignoreCase);
	~Searcher();

	Searcher(Searcher &&other);
	Searcher &operator=(Searcher &&other);

	bool IsRegex() const {return fRegex;}

	// The first match starting at or after from.
	std::optional<Match> Find(std::string_view text, size_t from) const;
	// The last match starting before `before`. Regex mode: of the
	// non-overlapping matches from the start of the text.
	std::optional<Match> FindLast(std::string_view text, size_t before) const;
	// The match starting exactly at from.
	std::optional<Match> MatchAt(std::string_view text, size_t from) const;
	// Non-overlapping matches lying entirely in [from, to], in text order.
	std::vector<Match> FindAll(std::string_view text, size_t from, size_t to) const;

	// The text replacing a match. Exact mode: the replacement as is. Regex
	// mode: $n, ${n} and \d (one digit d) insert a capture group (an unset or
	// missing group inserts nothing); \n and \t insert a newline and a tab;
	// \\, \$ and $$ a literal \ or $. Other characters are copied.
	std::string Expand(std::string_view text, const Match &match, std::string_view replacement) const;
};


// Text with CRLF and CR line endings converted to LF, as stored in models.
std::string NormalizeLineEndings(std::string_view text);

// Offset of the code point after the one at offset (at most text.size()).
size_t NextCodePoint(std::string_view text, size_t offset);


} // FindKit
