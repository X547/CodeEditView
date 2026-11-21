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


// JavaScript: "name(parameters)" of a function or an arrow function, whose
// single parameter may have no parentheses.
static std::string FunctionLabel(TSNode name, TSNode function, const Rope &text)
{
	std::string parameters = "()";
	const TSNode list = ChildByField(function, "parameters");
	const TSNode single = ChildByField(function, "parameter");
	if (!ts_node_is_null(list)) {
		parameters = NodeText(list, text);
	} else if (!ts_node_is_null(single)) {
		parameters = "(" + NodeText(single, text) + ")";
	}
	return Compact(NodeText(name, text) + parameters);
}


// JavaScript: a function value of a declaration or class field.
static bool IsFunctionValue(TSNode value)
{
	return !ts_node_is_null(value)
		&& (NodeType(value) == "arrow_function" || NodeType(value) == "function_expression");
}


static std::optional<OutlineItem> MakeItem(TSNode node, const Rope &text)
{
	OutlineItem item;
	item.start = ts_node_start_byte(node);
	item.end = ts_node_end_byte(node);
	const std::string_view type = NodeType(node);

	TSNode name = ChildByField(node, "name");
	const char *anonymous = nullptr;
	if (type == "function_definition" && !ts_node_is_null(name)) {
		// A shell or Python function, named directly.
		item.kind = OutlineKind::Function;
		const TSNode parameters = ChildByField(node, "parameters");
		item.label = Compact(NodeText(name, text)
			+ (ts_node_is_null(parameters) ? std::string("()") : NodeText(parameters, text)));
	} else if (type == "class_definition") {
		// Python
		item.kind = OutlineKind::Class;
	} else if (type == "function_def" || type == "macro_def") {
		// CMake: the first argument of function() or macro().
		item.kind = OutlineKind::Function;
		const TSNode command = ts_node_named_child(node, 0);
		const TSNode arguments = ts_node_is_null(command) ? command : ts_node_named_child(command, 1);
		if (ts_node_is_null(arguments) || NodeType(arguments) != "argument_list") {
			return std::nullopt;
		}
		name = ts_node_named_child(arguments, 0);
	} else if (type == "rule") {
		// Make: the targets.
		item.kind = OutlineKind::Function;
		name = ts_node_named_child(node, 0);
		if (ts_node_is_null(name) || NodeType(name) != "targets") {
			return std::nullopt;
		}
	} else if (type == "node") {
		// Devicetree: "label: name@address", or "&label" for a node changed
		// by reference.
		item.kind = OutlineKind::Struct;
		if (!ts_node_is_null(name)) {
			size_t end = ts_node_end_byte(name);
			const uint32_t count = ts_node_named_child_count(node);
			for (uint32_t i = 0; i < count; i++) {
				const TSNode child = ts_node_named_child(node, i);
				if (NodeType(child) == "unit_address") {
					end = ts_node_end_byte(child);
					break;
				}
			}
			const size_t start = ts_node_start_byte(name);
			item.label = Compact(text.Substring(start, end - start));
			const TSNode label = ChildByField(node, "label");
			if (!ts_node_is_null(label)) {
				item.label = NodeText(label, text) + ": " + item.label;
			}
		}
	} else if (type == "define_directive") {
		// Make
		item.kind = OutlineKind::Function;
	} else if (type == "function_definition") {
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
	} else if (type == "module") {
		// Component Pascal
		item.kind = OutlineKind::Namespace;
	} else if (type == "procedure_declaration") {
		// Component Pascal; methods are labelled with their receiver type.
		item.kind = OutlineKind::Function;
		const TSNode receiver = ChildByField(node, "receiver");
		if (!ts_node_is_null(name) && !ts_node_is_null(receiver)) {
			const TSNode receiverType = ChildByField(receiver, "type");
			if (!ts_node_is_null(receiverType)) {
				item.label = NodeText(receiverType, text) + "." + NodeText(name, text);
			}
		}
	} else if (type == "type_declaration") {
		// Component Pascal: record types and pointers to records.
		TSNode record = ChildByField(node, "type");
		if (!ts_node_is_null(record) && NodeType(record) == "pointer_type") {
			record = ChildByField(record, "target");
		}
		if (ts_node_is_null(record) || NodeType(record) != "record_type") {
			return std::nullopt;
		}
		const TSNode attribute = ChildByField(record, "attribute");
		const bool extensible = !ts_node_is_null(attribute)
			&& (NodeType(attribute) == "ABSTRACT" || NodeType(attribute) == "EXTENSIBLE");
		item.kind = extensible ? OutlineKind::Class : OutlineKind::Struct;
	} else if (type == "rule_definition") {
		// Jam
		item.kind = OutlineKind::Function;
	} else if (type == "actions_definition") {
		// Jam; usually named like the rule that runs it.
		item.kind = OutlineKind::Function;
		if (!ts_node_is_null(name)) {
			item.label = "actions " + Compact(NodeText(name, text));
		}
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
	} else if (type == "function_declaration" || type == "generator_function_declaration"
		|| type == "method_definition") {
		// JavaScript and TypeScript
		item.kind = OutlineKind::Function;
		if (!ts_node_is_null(name)) {
			item.label = FunctionLabel(name, node, text);
		}
	} else if (type == "lexical_declaration" || type == "variable_declaration") {
		// JavaScript: "const name = (parameters) => ...", named by the first
		// variable holding a function.
		item.kind = OutlineKind::Function;
		const uint32_t count = ts_node_named_child_count(node);
		for (uint32_t i = 0; i < count && ts_node_is_null(name); i++) {
			const TSNode declarator = ts_node_named_child(node, i);
			const TSNode value = ChildByField(declarator, "value");
			if (NodeType(declarator) == "variable_declarator" && IsFunctionValue(value)) {
				name = ChildByField(declarator, "name");
				item.label = FunctionLabel(name, value, text);
			}
		}
	} else if (type == "field_definition" || type == "public_field_definition") {
		// A class field holding a function; "property" in JavaScript, "name"
		// in TypeScript.
		item.kind = OutlineKind::Function;
		if (ts_node_is_null(name)) {
			name = ChildByField(node, "property");
		}
		const TSNode value = ChildByField(node, "value");
		if (ts_node_is_null(name) || !IsFunctionValue(value)) {
			return std::nullopt;
		}
		item.label = FunctionLabel(name, value, text);
	} else if (type == "class_declaration" || type == "abstract_class_declaration"
		|| type == "interface_declaration") {
		// JavaScript and TypeScript
		item.kind = OutlineKind::Class;
	} else if (type == "enum_declaration") {
		// TypeScript
		item.kind = OutlineKind::Enum;
	} else if (type == "type_alias_declaration") {
		// TypeScript
		item.kind = OutlineKind::Struct;
	} else if (type == "internal_module") {
		// TypeScript "namespace"; "module" is handled with Component Pascal.
		item.kind = OutlineKind::Namespace;
	} else if (type == "rule_set") {
		// CSS: the selectors.
		item.kind = OutlineKind::Struct;
		name = ts_node_named_child(node, 0);
		if (ts_node_is_null(name) || NodeType(name) != "selectors") {
			return std::nullopt;
		}
	} else if (type == "media_statement" || type == "supports_statement" || type == "keyframes_statement") {
		// CSS: the at-rule up to its block.
		item.kind = OutlineKind::Namespace;
		const uint32_t count = ts_node_named_child_count(node);
		if (count == 0) {
			return std::nullopt;
		}
		const TSNode block = ts_node_named_child(node, count - 1);
		if (NodeType(block) != "block" && NodeType(block) != "keyframe_block_list") {
			return std::nullopt;
		}
		const std::string header = text.Substring(item.start, ts_node_start_byte(block) - item.start);
		size_t length = header.size();
		while (length > 0 && isspace(static_cast<unsigned char>(header[length - 1]))) {
			length--;
		}
		item.label = Compact(header);
		item.nameStart = item.start;
		item.nameEnd = item.start + length;
		return item;
	} else {
		return std::nullopt;
	}

	if (ts_node_is_null(name)) {
		if (anonymous == nullptr) {
			// Incomplete code.
			return std::nullopt;
		}
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


//#pragma mark - Section marks

// A "#pragma mark" found at declaration level.
struct Mark {
	size_t start {};
	// The end of the preprocessor branch holding the mark, or SIZE_MAX.
	size_t branchEnd {};
	std::string label;
	size_t labelStart {};
	size_t labelEnd {};
};


// Nodes whose direct children are declarations; marks are taken only there.
static bool IsDeclarationContainer(std::string_view type)
{
	return type == "translation_unit" || type == "declaration_list"
		|| type == "field_declaration_list" || type == "preproc_if" || type == "preproc_ifdef"
		|| type == "preproc_else" || type == "preproc_elif" || type == "preproc_elifdef";
}


// Nodes that may hold declaration containers. Function bodies and
// expressions are not searched.
static bool MayHoldContainer(std::string_view type)
{
	return IsDeclarationContainer(type) || type == "namespace_definition"
		|| type == "linkage_specification" || type == "class_specifier"
		|| type == "struct_specifier" || type == "union_specifier"
		|| type == "template_declaration" || type == "declaration"
		|| type == "field_declaration" || type == "type_definition";
}


static size_t BranchEnd(TSNode container)
{
	if (!NodeType(container).starts_with("preproc_")) {
		return SIZE_MAX;
	}
	// An #else or #elif is a child of the branch before it.
	const TSNode alternative = ChildByField(container, "alternative");
	return ts_node_is_null(alternative) ? ts_node_end_byte(container) : ts_node_start_byte(alternative);
}


static bool IsBlank(char ch)
{
	return ch == ' ' || ch == '\t';
}


// Parses "#pragma mark <label>" or "// #pragma mark <label>"; the label
// loses surrounding spaces and dashes, so "#pragma mark -" has none.
static std::optional<Mark> ParseMark(TSNode node, const Rope &text)
{
	static constexpr size_t kMaxMarkLength = 512;
	const size_t start = ts_node_start_byte(node);
	std::string source = text.Substring(start, std::min<size_t>(ts_node_end_byte(node) - start, kMaxMarkLength));
	source.resize(std::min(source.size(), source.find_first_of("\r\n")));

	const std::string_view line(source);
	size_t pos = line.starts_with("//") ? 2 : 0;
	const auto skipBlanks = [&]() {
		while (pos < line.size() && IsBlank(line[pos])) {
			pos++;
		}
	};
	const auto skipWord = [&](std::string_view word) {
		if (line.substr(pos, word.size()) != word) {
			return false;
		}
		pos += word.size();
		return true;
	};
	skipBlanks();
	if (!skipWord("#")) {
		return std::nullopt;
	}
	skipBlanks();
	if (!skipWord("pragma") || pos >= line.size() || !IsBlank(line[pos])) {
		return std::nullopt;
	}
	skipBlanks();
	if (!skipWord("mark") || (pos < line.size() && !IsBlank(line[pos]) && line[pos] != '-')) {
		return std::nullopt;
	}

	size_t labelStart = pos;
	size_t labelEnd = line.size();
	while (labelStart < labelEnd && (IsBlank(line[labelStart]) || line[labelStart] == '-')) {
		labelStart++;
	}
	while (labelEnd > labelStart && (IsBlank(line[labelEnd - 1]) || line[labelEnd - 1] == '-')) {
		labelEnd--;
	}
	Mark mark;
	mark.start = start;
	mark.label = Compact(line.substr(labelStart, labelEnd - labelStart));
	mark.labelStart = start + labelStart;
	mark.labelEnd = start + labelEnd;
	return mark;
}


// Appends the marks below the cursor node in document order.
static void CollectMarks(TSTreeCursor &cursor, const Rope &text, std::vector<Mark> &marks)
{
	const TSNode node = ts_tree_cursor_current_node(&cursor);
	const bool container = IsDeclarationContainer(NodeType(node));
	const size_t branchEnd = container ? BranchEnd(node) : SIZE_MAX;
	if (!ts_tree_cursor_goto_first_child(&cursor)) {
		return;
	}
	do {
		const TSNode child = ts_tree_cursor_current_node(&cursor);
		const std::string_view type = NodeType(child);
		if (container && (type == "comment" || type == "preproc_call")) {
			if (std::optional<Mark> mark = ParseMark(child, text)) {
				mark->branchEnd = branchEnd;
				marks.push_back(std::move(*mark));
			}
		} else if (MayHoldContainer(type)) {
			CollectMarks(cursor, text, marks);
		}
	} while (ts_tree_cursor_goto_next_sibling(&cursor));
	ts_tree_cursor_goto_parent(&cursor);
}


// Groups items of one level (and recursively their children) into sections.
// Takes the marks starting before levelEnd; marks inside an item belong to
// its children.
static std::vector<OutlineItem> GroupSections(std::vector<OutlineItem> items, size_t levelEnd, const std::vector<Mark> &marks, size_t &index)
{
	std::vector<const Mark*> levelMarks;
	for (OutlineItem &item : items) {
		while (index < marks.size() && marks[index].start < item.start) {
			levelMarks.push_back(&marks[index++]);
		}
		item.children = GroupSections(std::move(item.children), item.end, marks, index);
	}
	while (index < marks.size() && marks[index].start < levelEnd) {
		levelMarks.push_back(&marks[index++]);
	}
	if (levelMarks.empty()) {
		return items;
	}

	std::vector<OutlineItem> result;
	size_t next = 0;
	for (size_t i = 0; i < levelMarks.size(); i++) {
		const Mark &mark = *levelMarks[i];
		const size_t nextMarkStart = i + 1 < levelMarks.size() ? levelMarks[i + 1]->start : levelEnd;
		while (next < items.size() && items[next].start < mark.start) {
			result.push_back(std::move(items[next++]));
		}

		if (mark.label.empty()) {
			// A divider only between items: not first, not followed directly
			// by another mark or the end of the level.
			const bool itemFollows = next < items.size() && items[next].start < nextMarkStart;
			if (result.empty() || !itemFollows) {
				continue;
			}
			OutlineItem separator;
			separator.kind = OutlineKind::Separator;
			separator.start = separator.end = mark.start;
			separator.nameStart = separator.nameEnd = mark.start;
			result.push_back(std::move(separator));
			continue;
		}

		OutlineItem section;
		section.kind = OutlineKind::Section;
		section.label = mark.label;
		section.start = mark.start;
		section.end = std::min({nextMarkStart, mark.branchEnd, levelEnd});
		section.nameStart = mark.labelStart;
		section.nameEnd = mark.labelEnd;
		while (next < items.size() && items[next].start < section.end) {
			section.children.push_back(std::move(items[next++]));
		}
		result.push_back(std::move(section));
	}
	while (next < items.size()) {
		result.push_back(std::move(items[next++]));
	}
	return result;
}


//#pragma mark - Outline

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
	std::vector<OutlineItem> items = Nest(flat, index, SIZE_MAX);

	const TSNode root = ts_tree_root_node(tree);
	std::vector<Mark> marks;
	TSTreeCursor treeCursor = ts_tree_cursor_new(root);
	CollectMarks(treeCursor, text, marks);
	ts_tree_cursor_delete(&treeCursor);
	size_t markIndex = 0;
	return GroupSections(std::move(items), ts_node_end_byte(root), marks, markIndex);
}


} // CodeEdit
