#pragma once

#include <stddef.h>

#include <vector>

#include <tree_sitter/api.h>

#include <CodeEditView/Theme.h>


namespace CodeEdit {


class Language;
class Rope;


class QueryCursor {
private:
	TSQueryCursor *fCursor;

public:
	QueryCursor():
		fCursor(ts_query_cursor_new())
	{
	}

	~QueryCursor() {ts_query_cursor_delete(fCursor);}

	QueryCursor(const QueryCursor &other) = delete;
	QueryCursor &operator=(const QueryCursor &other) = delete;

	TSQueryCursor *Get() const {return fCursor;}
};


// Fills the styles of bytes [start, start + styles.size()). For text covered
// by several captures, the later one wins.
void Highlight(
	const Language &language,
	const TSTree *tree,
	const Rope &text,
	QueryCursor &cursor,
	size_t start,
	std::vector<HighlightStyle> &styles
);

// Sorted start offsets of the definitions matched by the navigation query.
std::vector<size_t> DefinitionStarts(const Language &language, const TSTree *tree, QueryCursor &cursor);


} // CodeEdit
