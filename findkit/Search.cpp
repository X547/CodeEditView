#include "Search.h"

#include <algorithm>
#include <new>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>


namespace FindKit {


// A window never grows beyond this; a longer match throws SearchError.
static constexpr size_t kMaxWindowSize = 64 * 1024 * 1024;
static constexpr size_t kMinWindowSize = 16;
// Bytes of a code point at most.
static constexpr size_t kMaxCodePointBytes = 4;
// FindLast() looks this far back for a line start to begin at.
static constexpr size_t kMaxLineStartSearch = 4096;
static constexpr size_t kFirstBackwardBlock = 4096;


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


static bool IsContinuation(char c)
{
	return (static_cast<uint8_t>(c) & 0xC0) == 0x80;
}


static Match Shifted(Match match, ptrdiff_t delta)
{
	match.offset += delta;
	for (auto &[offset, length] : match.groups) {
		if (offset != kNoGroup) {
			offset += delta;
		}
	}
	return match;
}


// The start of the code point holding offset.
static size_t AlignBack(const TextSource &text, size_t offset)
{
	if (offset == 0 || offset >= text.Length()) {
		return std::min(offset, text.Length());
	}
	const size_t from = offset - std::min(offset, kMaxCodePointBytes - 1);
	std::string buffer;
	const std::string_view bytes = text.View(from, offset - from + 1, buffer);
	size_t pos = offset - from;
	while (pos > 0 && IsContinuation(bytes[pos])) {
		pos--;
	}
	return from + pos;
}


// Where to continue after a match so that matches do not overlap and an empty
// match does not repeat; past the end when there is nothing left.
static size_t NextSearchOffset(const TextSource &text, const Match &match)
{
	if (match.length > 0) {
		return match.End();
	}
	if (match.offset >= text.Length()) {
		return text.Length() + 1;
	}
	return NextCodePoint(text, match.offset);
}


//#pragma mark - StringSource

StringSource::StringSource(std::string_view text, size_t windowSize):
	fText(text),
	fWindowSize(windowSize)
{
}


std::string_view StringSource::View(size_t offset, size_t length, std::string &buffer) const
{
	(void)buffer;
	return fText.substr(offset, length);
}


//#pragma mark - Scanner

// Searches a text source through a window that moves forward with the
// search, and grows while a match might continue past its end.
class Scanner {
private:
	const Searcher &fSearcher;
	const TextSource &fText;
	MatchData fMatchData;
	std::string fBuffer;
	std::string_view fWindow;
	size_t fWindowStart {};
	size_t fWindowSize;
	bool fLoaded {};

	size_t WindowEnd() const {return fWindowStart + fWindow.size();}
	bool Holds(size_t from) const;
	void Load(size_t from);

public:
	Scanner(const Searcher &searcher, const TextSource &text);

	// Matches starting after stopAt are not looked for, but may be found.
	std::optional<Match> Find(size_t from, bool anchored, size_t stopAt = SIZE_MAX);
};


Scanner::Scanner(const Searcher &searcher, const TextSource &text):
	fSearcher(searcher),
	fText(text),
	fMatchData(searcher.fCode != nullptr ? searcher.fCode->code : nullptr),
	fWindowSize(std::clamp(text.WindowSize(), kMinWindowSize, kMaxWindowSize))
{
}


// The window holds from with its context before.
bool Scanner::Holds(size_t from) const
{
	if (!fLoaded || from < fWindowStart || from > WindowEnd()) {
		return false;
	}
	const bool contextInside = fWindowStart == 0
		|| from - fWindowStart >= fSearcher.fContextBytes + kMaxCodePointBytes;
	return contextInside && (from < WindowEnd() || WindowEnd() == fText.Length());
}


// Windows start and end at code points, or a character could be cut.
void Scanner::Load(size_t from)
{
	const size_t length = fText.Length();
	const size_t context = fSearcher.fContextBytes + kMaxCodePointBytes;
	const size_t start = AlignBack(fText, from - std::min(from, context));
	size_t end = from + std::min(fWindowSize, length - from);
	if (end < length) {
		const size_t aligned = AlignBack(fText, end);
		if (aligned > from) {
			end = aligned;
		}
	}
	fWindow = fText.View(start, end - start, fBuffer);
	fWindowStart = start;
	fLoaded = true;
}


std::optional<Match> Scanner::Find(size_t from, bool anchored, size_t stopAt)
{
	const size_t length = fText.Length();
	if (from > length) {
		return std::nullopt;
	}
	size_t progress = SIZE_MAX;
	for (;;) {
		if (!Holds(from)) {
			Load(from);
		}
		const bool atEnd = WindowEnd() == length;
		const Searcher::WindowResult result = fSearcher.SearchWindow(
			fWindow,
			from - fWindowStart,
			atEnd,
			anchored,
			fMatchData.Get()
		);
		switch (result.kind) {
			case Searcher::WindowResultKind::Found:
				return Shifted(result.match, fWindowStart);
			case Searcher::WindowResultKind::NotFound:
				if (atEnd || anchored || WindowEnd() > stopAt) {
					return std::nullopt;
				}
				from = WindowEnd();
				break;
			case Searcher::WindowResultKind::Partial: {
				const size_t partialStart = fWindowStart + result.partialStart;
				if (partialStart > stopAt) {
					return std::nullopt;
				}
				if (partialStart == progress) {
					// The match may be longer than the window.
					if (fWindowSize >= kMaxWindowSize) {
						throw SearchError("A match is longer than 64 MiB");
					}
					fWindowSize = std::min(2 * fWindowSize, kMaxWindowSize);
				}
				progress = partialStart;
				from = partialStart;
				fLoaded = false;
				break;
			}
		}
	}
}


//#pragma mark - Searcher

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
	pcre2_jit_compile(code, PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_HARD);

	// Lookbehind counts characters; \b and ^ look at one.
	uint32_t lookbehind = 0;
	pcre2_pattern_info(code, PCRE2_INFO_MAXLOOKBEHIND, &lookbehind);
	fContextBytes = (std::max<uint32_t>(lookbehind, 1) + 1) * kMaxCodePointBytes;
}


Searcher::~Searcher() = default;
Searcher::Searcher(Searcher &&other) = default;
Searcher &Searcher::operator=(Searcher &&other) = default;


// Unless atEnd, the text continues after the window: a match reaching its
// end might continue and is reported as partial.
Searcher::WindowResult Searcher::SearchWindow(std::string_view window, size_t from, bool atEnd, bool anchored,
	pcre2_match_data *matchData) const
{
	WindowResult result;
	if (fCode == nullptr) {
		const size_t offset = anchored
			? (window.substr(from, fPattern.size()) == fPattern ? from : std::string_view::npos)
			: window.find(fPattern, from);
		if (offset != std::string_view::npos) {
			result.kind = WindowResultKind::Found;
			result.match = Match {offset, fPattern.size(), {}};
			return result;
		}
		// The pattern may start in the last bytes.
		const size_t tail = window.size() - std::min(window.size(), fPattern.size() - 1);
		if (atEnd || (anchored && from + fPattern.size() <= window.size())) {
			result.kind = WindowResultKind::NotFound;
		} else {
			result.kind = WindowResultKind::Partial;
			result.partialStart = anchored ? from : std::max(from, tail);
		}
		return result;
	}

	uint32_t options = anchored ? PCRE2_ANCHORED : 0;
	if (!atEnd) {
		options |= PCRE2_PARTIAL_HARD;
	}
	const int code = pcre2_match(
		fCode->code,
		reinterpret_cast<PCRE2_SPTR>(window.data()),
		window.size(),
		from,
		options,
		matchData,
		nullptr
	);
	if (code == PCRE2_ERROR_NOMATCH) {
		result.kind = WindowResultKind::NotFound;
		return result;
	}
	const PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(matchData);
	if (code == PCRE2_ERROR_PARTIAL) {
		result.kind = WindowResultKind::Partial;
		result.partialStart = std::max<size_t>(ovector[0], from);
		return result;
	}
	if (code < 0) {
		throw SearchError(ErrorMessage(code));
	}

	const uint32_t count = pcre2_get_ovector_count(matchData);
	Match &match = result.match;
	result.kind = WindowResultKind::Found;
	match.offset = ovector[0];
	// \K in a lookahead can end a match before its start.
	match.length = ovector[1] > ovector[0] ? ovector[1] - ovector[0] : 0;
	match.groups.reserve(count);
	for (uint32_t i = 0; i < count; i++) {
		const PCRE2_SIZE start = ovector[2 * i];
		const PCRE2_SIZE end = ovector[2 * i + 1];
		if (static_cast<int>(i) >= code || start == PCRE2_UNSET || end < start) {
			match.groups.emplace_back(kNoGroup, 0);
		} else {
			match.groups.emplace_back(start, end - start);
		}
	}
	return result;
}


std::optional<Match> Searcher::Find(const TextSource &text, size_t from) const
{
	Scanner scanner(*this, text);
	return scanner.Find(from, false);
}


std::optional<Match> Searcher::MatchAt(const TextSource &text, size_t from) const
{
	Scanner scanner(*this, text);
	return scanner.Find(from, true);
}


// Exact search goes backward through windows overlapping by the pattern
// length. Regex search scans forward from a line start before `before`,
// doubling the distance until it finds a match.
std::optional<Match> Searcher::FindLast(const TextSource &text, size_t before) const
{
	const size_t length = text.Length();
	before = std::min(before, length + 1);
	if (before == 0) {
		return std::nullopt;
	}

	if (fCode == nullptr) {
		const size_t windowSize = std::max(std::min(text.WindowSize(), kMaxWindowSize), 2 * fPattern.size());
		std::string buffer;
		size_t end = std::min(length, before - 1 + fPattern.size());
		for (;;) {
			const size_t start = end - std::min(end, windowSize);
			const std::string_view window = text.View(start, end - start, buffer);
			if (before - 1 >= start) {
				const size_t offset = window.rfind(fPattern, before - 1 - start);
				if (offset != std::string_view::npos) {
					return Match {start + offset, fPattern.size(), {}};
				}
			}
			if (start == 0) {
				return std::nullopt;
			}
			end = start + fPattern.size() - 1;
		}
	}

	Scanner scanner(*this, text);
	size_t limit = before;
	size_t block = kFirstBackwardBlock;
	for (;;) {
		size_t blockStart = limit - std::min(limit, block);
		if (blockStart > 0) {
			// Back to a line start if one is near.
			const size_t searchStart = blockStart - std::min(blockStart, kMaxLineStartSearch);
			std::string buffer;
			const std::string_view bytes = text.View(searchStart, blockStart - searchStart, buffer);
			const size_t newline = bytes.rfind('\n');
			blockStart = newline != std::string_view::npos
				? searchStart + newline + 1 : AlignBack(text, blockStart);
		}

		std::optional<Match> last;
		size_t pos = blockStart;
		while (std::optional<Match> match = scanner.Find(pos, false, limit)) {
			if (match->offset >= limit) {
				break;
			}
			pos = NextSearchOffset(text, *match);
			last = std::move(match);
		}
		if (last.has_value() || blockStart == 0) {
			return last;
		}
		limit = blockStart;
		block = std::min(2 * block, kMaxWindowSize);
	}
}


std::vector<Match> Searcher::FindAll(const TextSource &text, size_t from, size_t to) const
{
	Scanner scanner(*this, text);
	std::vector<Match> matches;
	size_t offset = from;
	while (offset <= to) {
		std::optional<Match> match = scanner.Find(offset, false, to);
		if (!match.has_value() || match->offset > to) {
			break;
		}
		if (match->End() > to) {
			// Crosses the end of the range; a shorter match may start later.
			if (match->offset >= text.Length()) {
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


// Only the text the groups cover is read.
std::string Searcher::Expand(const TextSource &text, const Match &match, std::string_view replacement) const
{
	if (!fRegex) {
		return std::string(replacement);
	}
	size_t start = match.offset;
	size_t end = match.End();
	for (const auto &[offset, length] : match.groups) {
		if (offset != kNoGroup) {
			start = std::min(start, offset);
			end = std::max(end, offset + length);
		}
	}
	std::string buffer;
	const std::string_view span = text.View(start, end - start, buffer);
	return Expand(span, Shifted(match, -static_cast<ptrdiff_t>(start)), replacement);
}


std::optional<Match> Searcher::Find(std::string_view text, size_t from) const
{
	return Find(StringSource(text), from);
}


std::optional<Match> Searcher::FindLast(std::string_view text, size_t before) const
{
	return FindLast(StringSource(text), before);
}


std::optional<Match> Searcher::MatchAt(std::string_view text, size_t from) const
{
	return MatchAt(StringSource(text), from);
}


std::vector<Match> Searcher::FindAll(std::string_view text, size_t from, size_t to) const
{
	return FindAll(StringSource(text), from, to);
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


//#pragma mark - Functions

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
	while (offset < text.size() && IsContinuation(text[offset])) {
		offset++;
	}
	return offset;
}


size_t NextCodePoint(const TextSource &text, size_t offset)
{
	const size_t length = text.Length();
	if (offset >= length) {
		return length;
	}
	std::string buffer;
	const std::string_view bytes = text.View(offset, std::min(length - offset, kMaxCodePointBytes), buffer);
	return offset + NextCodePoint(bytes, 0);
}


} // FindKit
