#include "Highlighter.h"

#include <stdint.h>

#include <algorithm>

#include "Language.h"


namespace CodeEdit {


void Highlight(
	const Language &language,
	const TSTree *tree,
	const Rope &text,
	QueryCursor &cursor,
	size_t start,
	std::vector<HighlightStyle> &styles
)
{
	std::fill(styles.begin(), styles.end(), HighlightStyle::Default);
	if (styles.empty()) {
		return;
	}

	const uint32_t rangeStart = static_cast<uint32_t>(start);
	const uint32_t rangeEnd = static_cast<uint32_t>(start + styles.size());
	ts_query_cursor_set_byte_range(cursor.Get(), rangeStart, rangeEnd);
	ts_query_cursor_exec(cursor.Get(), language.Highlights(), ts_tree_root_node(tree));

	TSQueryMatch match;
	uint32_t captureIndex;
	while (ts_query_cursor_next_capture(cursor.Get(), &match, &captureIndex)) {
		const TSQueryCapture &capture = match.captures[captureIndex];
		const HighlightStyle style = language.CaptureStyle(capture.index);
		if (style == HighlightStyle::Default || !language.MatchesPredicates(match, text)) {
			continue;
		}
		const uint32_t nodeStart = std::max(ts_node_start_byte(capture.node), rangeStart);
		const uint32_t nodeEnd = std::min(ts_node_end_byte(capture.node), rangeEnd);
		for (uint32_t offset = nodeStart; offset < nodeEnd; offset++) {
			styles[offset - rangeStart] = style;
		}
	}
}


std::vector<size_t> DefinitionStarts(const Language &language, const TSTree *tree, QueryCursor &cursor)
{
	std::vector<size_t> starts;
	ts_query_cursor_set_byte_range(cursor.Get(), 0, UINT32_MAX);
	ts_query_cursor_exec(cursor.Get(), language.Navigation(), ts_tree_root_node(tree));

	TSQueryMatch match;
	uint32_t captureIndex;
	while (ts_query_cursor_next_capture(cursor.Get(), &match, &captureIndex)) {
		starts.push_back(ts_node_start_byte(match.captures[captureIndex].node));
	}
	std::sort(starts.begin(), starts.end());
	starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
	return starts;
}


} // CodeEdit
