#include "Outline.h"

#include <ctype.h>
#include <string.h>

#include <algorithm>
#include <optional>
#include <string_view>

#include <CodeEditView/Rope.h>

#include "Highlighter.h"
#include "Language.h"


namespace CodeEdit {


static constexpr size_t kMaxLabelLength = 120;


static std::string_view NodeType(TSNode node)
{
	return ts_node_type(node);
}


static TSNode ChildByField(TSNode node, const char *field)
{
	return ts_node_child_by_field_name(node, field, static_cast<uint32_t>(strlen(field)));
}


static std::string NodeText(TSNode node, const Rope &text)
{
	const size_t start = ts_node_start_byte(node);
	return text.Substring(start, ts_node_end_byte(node) - start);
}


// Text on one line: whitespace runs become one space, none after an opening
// or before a closing parenthesis.
static std::string Compact(std::string_view source)
{
	std::string result;
	bool pendingSpace = false;
	for (const char ch : source) {
		if (isspace(static_cast<unsigned char>(ch))) {
			pendingSpace = !result.empty();
			continue;
		}
		if (pendingSpace && result.back() != '(' && ch != ')' && ch != ',') {
			result += ' ';
		}
		pendingSpace = false;
		result += ch;
	}
	if (result.size() > kMaxLabelLength) {
		result.resize(kMaxLabelLength);
		result += "\xe2\x80\xa6";
	}
	return result;
}


// The function declarator of a function definition, below pointer and
// reference declarators of the return type.
static std::optional<TSNode> FunctionDeclarator(TSNode definition)
{
	TSNode declarator = ChildByField(definition, "declarator");
	while (!ts_node_is_null(declarator)) {
		if (NodeType(declarator) == "function_declarator") {
			return declarator;
		}
		TSNode next = ChildByField(declarator, "declarator");
		if (ts_node_is_null(next)) {
			// reference_declarator has no field for its declarator.
			const uint32_t count = ts_node_named_child_count(declarator);
			if (count == 0) {
				break;
			}
			next = ts_node_named_child(declarator, count - 1);
		}
		declarator = next;
	}
	return std::nullopt;
}


static std::optional<OutlineItem> MakeItem(TSNode node, const Rope &text)
{
	OutlineItem item;
	item.start = ts_node_start_byte(node);
	item.end = ts_node_end_byte(node);
	const std::string_view type = NodeType(node);

	TSNode name = ChildByField(node, "name");
	const char *anonymous = nullptr;
	if (type == "function_definition") {
		item.kind = OutlineKind::Function;
		const std::optional<TSNode> declarator = FunctionDeclarator(node);
		if (!declarator.has_value()) {
			return std::nullopt;
		}
		name = ChildByField(*declarator, "declarator");
		if (ts_node_is_null(name)) {
			return std::nullopt;
		}
		const TSNode parameters = ChildByField(*declarator, "parameters");
		item.label = Compact(NodeText(name, text)
			+ (ts_node_is_null(parameters) ? std::string() : NodeText(parameters, text)));
	} else if (type == "namespace_definition") {
		item.kind = OutlineKind::Namespace;
		anonymous = "(anonymous namespace)";
	} else if (type == "class_specifier") {
		item.kind = OutlineKind::Class;
		anonymous = "(anonymous class)";
	} else if (type == "struct_specifier") {
		item.kind = OutlineKind::Struct;
		anonymous = "(anonymous struct)";
	} else if (type == "union_specifier") {
		item.kind = OutlineKind::Union;
		anonymous = "(anonymous union)";
	} else if (type == "enum_specifier") {
		item.kind = OutlineKind::Enum;
		anonymous = "(anonymous enum)";
	} else {
		return std::nullopt;
	}

	if (ts_node_is_null(name)) {
		item.label = anonymous;
		item.nameStart = item.nameEnd = item.start;
		return item;
	}
	if (item.label.empty()) {
		item.label = Compact(NodeText(name, text));
	}
	item.nameStart = ts_node_start_byte(name);
	item.nameEnd = ts_node_end_byte(name);
	return item;
}


// Moves items from flat (sorted by start, outer first) into a tree; takes
// the items starting before end.
static std::vector<OutlineItem> Nest(std::vector<OutlineItem> &flat, size_t &index, size_t end)
{
	std::vector<OutlineItem> items;
	while (index < flat.size() && flat[index].start < end) {
		OutlineItem item = std::move(flat[index++]);
		item.children = Nest(flat, index, item.end);
		items.push_back(std::move(item));
	}
	return items;
}


std::vector<OutlineItem> BuildOutline(const Language &language, const TSTree *tree, const Rope &text, QueryCursor &cursor)
{
	std::vector<OutlineItem> flat;
	ts_query_cursor_set_byte_range(cursor.Get(), 0, UINT32_MAX);
	ts_query_cursor_exec(cursor.Get(), language.Navigation(), ts_tree_root_node(tree));
	TSQueryMatch match;
	uint32_t captureIndex;
	while (ts_query_cursor_next_capture(cursor.Get(), &match, &captureIndex)) {
		if (std::optional<OutlineItem> item = MakeItem(match.captures[captureIndex].node, text)) {
			flat.push_back(std::move(*item));
		}
	}

	std::sort(flat.begin(), flat.end(), [](const OutlineItem &a, const OutlineItem &b) {
		return a.start != b.start ? a.start < b.start : a.end > b.end;
	});
	flat.erase(
		std::unique(flat.begin(), flat.end(), [](const OutlineItem &a, const OutlineItem &b) {
			return a.start == b.start && a.end == b.end && a.kind == b.kind;
		}),
		flat.end()
	);
	size_t index = 0;
	return Nest(flat, index, SIZE_MAX);
}


} // CodeEdit
