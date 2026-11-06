#include "Search.h"

#include <new>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>


namespace FindKit {


struct Searcher::Code {
	pcre2_code *code {};

	explicit Code(pcre2_code *compiled):
		code(compiled)
	{
	}

	~Code() {pcre2_code_free(code);}

	Code(const Code &other) = delete;
	Code &operator=(const Code &other) = delete;
};


// Match data for one search; none for a plain byte search.
class MatchData {
private:
	pcre2_match_data *fData {};

public:
	explicit MatchData(const pcre2_code *code)
	{
		if (code == nullptr) {
			return;
		}
		fData = pcre2_match_data_create_from_pattern(code, nullptr);
		if (fData == nullptr) {
			throw std::bad_alloc();
		}
	}

	~MatchData() {pcre2_match_data_free(fData);}

	MatchData(const MatchData &other) = delete;
	MatchData &operator=(const MatchData &other) = delete;

	pcre2_match_data *Get() const {return fData;}
};


static std::string ErrorMessage(int errorCode)
{
	PCRE2_UCHAR buffer[256];
	if (pcre2_get_error_message(errorCode, buffer, sizeof(buffer)) < 0) {
		return "Unknown error " + std::to_string(errorCode);
	}
	return reinterpret_cast<const char*>(buffer);
}


static bool IsDigit(char c)
{
	return c >= '0' && c <= '9';
}


// Where to continue after a match so that matches do not overlap and an empty
// match does not repeat; text.size() + 1 when there is nothing left.
static size_t NextSearchOffset(std::string_view text, const Match &match)
{
	if (match.length > 0) {
		return match.End();
	}
	if (match.offset >= text.size()) {
		return text.size() + 1;
	}
	return NextCodePoint(text, match.offset);
}


Searcher::Searcher(std::string_view pattern, bool regex, bool ignoreCase):
	fPattern(pattern),
	fRegex(regex)
{
	if (pattern.empty()) {
		throw PatternError("Empty search string", 0);
	}
	if (!regex && !ignoreCase) {
		return;
	}

	uint32_t options = PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;
	if (ignoreCase) {
		options |= PCRE2_CASELESS;
	}
	if (regex) {
		// ALT_CIRCUMFLEX: ^ also matches on an empty last line.
		options |= PCRE2_UCP | PCRE2_MULTILINE | PCRE2_ALT_CIRCUMFLEX;
	} else {
		options |= PCRE2_LITERAL;
	}

	pcre2_compile_context *context = pcre2_compile_context_create(nullptr);
	if (context == nullptr) {
		throw std::bad_alloc();
	}
	pcre2_set_newline(context, PCRE2_NEWLINE_LF);
	int errorCode {};
	PCRE2_SIZE errorOffset {};
	pcre2_code *code = pcre2_compile(
		reinterpret_cast<PCRE2_SPTR>(pattern.data()),
		pattern.size(),
		options,
		&errorCode,
		&errorOffset,
		context
	);
	pcre2_compile_context_free(context);
	if (code == nullptr) {
		throw PatternError(ErrorMessage(errorCode), errorOffset);
	}
	fCode = std::make_unique<Code>(code);
	// Falls back to the interpreter where JIT is not available.
	pcre2_jit_compile(code, PCRE2_JIT_COMPLETE);
}


Searcher::~Searcher() = default;
Searcher::Searcher(Searcher &&other) = default;
Searcher &Searcher::operator=(Searcher &&other) = default;


std::optional<Match> Searcher::Run(std::string_view text, size_t from, bool anchored, pcre2_match_data *matchData) const
{
	if (from > text.size()) {
		return std::nullopt;
	}
	const int result = pcre2_match(
		fCode->code,
		reinterpret_cast<PCRE2_SPTR>(text.data()),
		text.size(),
		from,
		anchored ? PCRE2_ANCHORED : 0,
		matchData,
		nullptr
	);
	if (result == PCRE2_ERROR_NOMATCH) {
		return std::nullopt;
	}
	if (result < 0) {
		throw SearchError(ErrorMessage(result));
	}

	const PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(matchData);
	const uint32_t count = pcre2_get_ovector_count(matchData);
	Match match;
	match.offset = ovector[0];
	// \K in a lookahead can end a match before its start.
	match.length = ovector[1] > ovector[0] ? ovector[1] - ovector[0] : 0;
	match.groups.reserve(count);
	for (uint32_t i = 0; i < count; i++) {
		const PCRE2_SIZE start = ovector[2 * i];
		const PCRE2_SIZE end = ovector[2 * i + 1];
		if (static_cast<int>(i) >= result || start == PCRE2_UNSET || end < start) {
			match.groups.emplace_back(kNoGroup, 0);
		} else {
			match.groups.emplace_back(start, end - start);
		}
	}
	return match;
}


std::optional<Match> Searcher::FindWith(std::string_view text, size_t from, pcre2_match_data *matchData) const
{
	if (fCode != nullptr) {
		return Run(text, from, false, matchData);
	}
	if (from > text.size()) {
		return std::nullopt;
	}
	const size_t offset = text.find(fPattern, from);
	if (offset == std::string_view::npos) {
		return std::nullopt;
	}
	return Match {offset, fPattern.size(), {}};
}


std::optional<Match> Searcher::Find(std::string_view text, size_t from) const
{
	const MatchData matchData(fCode != nullptr ? fCode->code : nullptr);
	return FindWith(text, from, matchData.Get());
}


std::optional<Match> Searcher::FindLast(std::string_view text, size_t before) const
{
	if (fCode == nullptr) {
		if (before == 0) {
			return std::nullopt;
		}
		const size_t offset = text.rfind(fPattern, before - 1);
		if (offset == std::string_view::npos) {
			return std::nullopt;
		}
		return Match {offset, fPattern.size(), {}};
	}

	const MatchData matchData(fCode->code);
	std::optional<Match> last;
	size_t offset = 0;
	while (offset <= text.size()) {
		std::optional<Match> match = FindWith(text, offset, matchData.Get());
		if (!match.has_value() || match->offset >= before) {
			break;
		}
		offset = NextSearchOffset(text, *match);
		last = std::move(match);
	}
	return last;
}


std::optional<Match> Searcher::MatchAt(std::string_view text, size_t from) const
{
	if (fCode != nullptr) {
		const MatchData matchData(fCode->code);
		return Run(text, from, true, matchData.Get());
	}
	if (from > text.size() || text.substr(from, fPattern.size()) != fPattern) {
		return std::nullopt;
	}
	return Match {from, fPattern.size(), {}};
}


std::vector<Match> Searcher::FindAll(std::string_view text, size_t from, size_t to) const
{
	const MatchData matchData(fCode != nullptr ? fCode->code : nullptr);
	std::vector<Match> matches;
	size_t offset = from;
	while (offset <= to) {
		std::optional<Match> match = FindWith(text, offset, matchData.Get());
		if (!match.has_value() || match->offset > to) {
			break;
		}
		if (match->End() > to) {
			// Crosses the end of the range; a shorter match may start later.
			if (match->offset >= text.size()) {
				break;
			}
			offset = NextCodePoint(text, match->offset);
			continue;
		}
		offset = NextSearchOffset(text, *match);
		matches.push_back(std::move(*match));
	}
	return matches;
}


std::string Searcher::Expand(std::string_view text, const Match &match, std::string_view replacement) const
{
	if (!fRegex) {
		return std::string(replacement);
	}

	std::string result;
	const auto appendGroup = [&](size_t index) {
		if (index < match.groups.size() && match.groups[index].first != kNoGroup) {
			result.append(text.substr(match.groups[index].first, match.groups[index].second));
		}
	};

	size_t i = 0;
	while (i < replacement.size()) {
		const char c = replacement[i];
		const char next = i + 1 < replacement.size() ? replacement[i + 1] : '\0';
		if (c == '\\' && next != '\0') {
			i += 2;
			if (IsDigit(next)) {
				appendGroup(next - '0');
				continue;
			}
			switch (next) {
				case 'n':
					result += '\n';
					break;
				case 't':
					result += '\t';
					break;
				case '\\':
				case '$':
					result += next;
					break;
				default:
					result += c;
					result += next;
					break;
			}
			continue;
		}
		if (c == '$' && next == '$') {
			result += '$';
			i += 2;
			continue;
		}
		if (c == '$' && IsDigit(next)) {
			size_t index = next - '0';
			i += 2;
			// A second digit only if that group exists: "$10" with one group
			// is group 1 followed by "0".
			if (i < replacement.size() && IsDigit(replacement[i])
				&& index * 10 + (replacement[i] - '0') < match.groups.size()) {
				index = index * 10 + (replacement[i] - '0');
				i++;
			}
			appendGroup(index);
			continue;
		}
		if (c == '$' && next == '{') {
			const size_t close = replacement.find('}', i + 2);
			const std::string_view digits = close == std::string_view::npos
				? std::string_view() : replacement.substr(i + 2, close - i - 2);
			bool valid = !digits.empty() && digits.size() <= 4;
			size_t index = 0;
			for (const char digit: digits) {
				valid = valid && IsDigit(digit);
				index = index * 10 + (digit - '0');
			}
			if (valid) {
				appendGroup(index);
				i = close + 1;
				continue;
			}
		}
		result += c;
		i++;
	}
	return result;
}


std::string NormalizeLineEndings(std::string_view text)
{
	std::string result;
	result.reserve(text.size());
	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] != '\r') {
			result += text[i];
		} else if (i + 1 >= text.size() || text[i + 1] != '\n') {
			result += '\n';
		}
	}
	return result;
}


size_t NextCodePoint(std::string_view text, size_t offset)
{
	if (offset >= text.size()) {
		return text.size();
	}
	offset++;
	while (offset < text.size() && (static_cast<uint8_t>(text[offset]) & 0xC0) == 0x80) {
		offset++;
	}
	return offset;
}


} // FindKit
