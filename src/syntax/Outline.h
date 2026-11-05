#pragma once

#include <vector>

#include <tree_sitter/api.h>

#include <CodeEditView/Outline.h>


namespace CodeEdit {


class Language;
class QueryCursor;
class Rope;


// Definitions matched by the navigation query, nested by containment, and
// grouped by "#pragma mark" sections at declaration level.
std::vector<OutlineItem> BuildOutline(const Language &language, const TSTree *tree, const Rope &text, QueryCursor &cursor);


} // CodeEdit
