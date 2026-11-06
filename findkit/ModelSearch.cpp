#include "ModelSearch.h"

#include <stddef.h>

#include <algorithm>
#include <string>
#include <vector>


namespace FindKit {


using CodeEdit::EditKind;
using CodeEdit::ModelLock;


std::optional<Match> FindNext(
	const Searcher &searcher,
	std::string_view text,
	const Selection &selection,
	bool backward,
	bool wrap
)
{
	const size_t start = std::min(selection.anchor, selection.caret);
	const size_t end = std::max(selection.anchor, selection.caret);

	if (backward) {
		std::optional<Match> match = searcher.FindLast(text, start);
		if (!match.has_value() && wrap) {
			match = searcher.FindLast(text, text.size() + 1);
		}
		return match;
	}

	std::optional<Match> match = searcher.Find(text, end);
	if (match.has_value() && match->length == 0 && match->offset == start && start == end) {
		match = end < text.size() ? searcher.Find(text, NextCodePoint(text, end)) : std::nullopt;
	}
	if (!match.has_value() && wrap) {
		match = searcher.Find(text, 0);
	}
	return match;
}


std::optional<Match> SelectionMatch(const Searcher &searcher, std::string_view text, const Selection &selection)
{
	const size_t start = std::min(selection.anchor, selection.caret);
	const size_t end = std::max(selection.anchor, selection.caret);
	if (start == end) {
		return std::nullopt;
	}
	std::optional<Match> match = searcher.MatchAt(text, start);
	if (!match.has_value() || match->End() != end) {
		return std::nullopt;
	}
	return match;
}


std::optional<Selection> ReplaceSelection(
	TextModel &model,
	ObserverId owner,
	const Searcher &searcher,
	std::string_view replacement,
	const Selection &selection
)
{
	ModelLock lock(model);
	const std::string text = model.Text().ToString();
	const std::optional<Match> match = SelectionMatch(searcher, text, selection);
	if (!match.has_value()) {
		return std::nullopt;
	}

	const std::string expanded = searcher.Expand(text, *match, replacement);
	const Selection after {match->offset, match->offset + expanded.size()};
	model.BeginGroup(owner, EditKind::Other, selection);
	model.Replace(match->offset, match->length, expanded);
	model.EndGroup(after);
	return after;
}


ReplaceAllResult ReplaceAll(
	TextModel &model,
	ObserverId owner,
	const Searcher &searcher,
	std::string_view replacement,
	const Selection &selection,
	bool inSelection
)
{
	ModelLock lock(model);
	const std::string text = model.Text().ToString();
	size_t from = 0;
	size_t to = text.size();
	if (inSelection) {
		from = std::min(selection.anchor, selection.caret);
		to = std::max(selection.anchor, selection.caret);
		if (from == to) {
			return {0, selection};
		}
	}

	const std::vector<Match> matches = searcher.FindAll(text, from, to);
	if (matches.empty()) {
		return {0, selection};
	}

	std::vector<std::string> expanded;
	expanded.reserve(matches.size());
	for (const Match &match: matches) {
		expanded.push_back(searcher.Expand(text, match, replacement));
	}

	// Length change by all replacements before the last one, and in total.
	ptrdiff_t deltaBeforeLast = 0;
	for (size_t i = 0; i + 1 < matches.size(); i++) {
		deltaBeforeLast += static_cast<ptrdiff_t>(expanded[i].size()) - static_cast<ptrdiff_t>(matches[i].length);
	}
	const Match &last = matches.back();
	const size_t lastEnd = last.offset + deltaBeforeLast + expanded.back().size();
	const ptrdiff_t delta = deltaBeforeLast
		+ static_cast<ptrdiff_t>(expanded.back().size()) - static_cast<ptrdiff_t>(last.length);

	const Selection after = inSelection ? Selection {from, to + delta} : Selection {lastEnd, lastEnd};

	model.BeginGroup(owner, EditKind::Other, selection);
	for (size_t i = matches.size(); i-- > 0;) {
		model.Replace(matches[i].offset, matches[i].length, expanded[i]);
	}
	model.EndGroup(after);
	return {matches.size(), after};
}


} // FindKit
