#include "Language.h"

#include <ctype.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include <CodeEditView/Language.h>
#include <CodeEditView/Rope.h>

#include "support/Assert.h"

#include "CHighlights.inc"
#include "CMakeHighlights.inc"
#include "ComponentPascalHighlights.inc"
#include "CppHighlights.inc"
#include "JamHighlights.inc"
#include "MakeHighlights.inc"
#include "MesonHighlights.inc"
#include "PythonHighlights.inc"
#include "ShellHighlights.inc"


extern "C" const TSLanguage *tree_sitter_c();
extern "C" const TSLanguage *tree_sitter_cpp();
extern "C" const TSLanguage *tree_sitter_bash();
extern "C" const TSLanguage *tree_sitter_meson();
extern "C" const TSLanguage *tree_sitter_jam();
extern "C" const TSLanguage *tree_sitter_componentpascal();
extern "C" const TSLanguage *tree_sitter_python();
extern "C" const TSLanguage *tree_sitter_make();
extern "C" const TSLanguage *tree_sitter_cmake();


namespace CodeEdit {


static constexpr const char *kCNavigation = R"scm(
(function_definition) @definition
(struct_specifier body: (_)) @definition
(union_specifier body: (_)) @definition
(enum_specifier body: (_)) @definition
)scm";

static constexpr const char *kCppNavigation = R"scm(
(function_definition) @definition
(struct_specifier body: (_)) @definition
(union_specifier body: (_)) @definition
(enum_specifier body: (_)) @definition
(class_specifier body: (_)) @definition
(namespace_definition) @definition
)scm";

static constexpr const char *kShellNavigation = R"scm(
(function_definition) @definition
)scm";

// Meson has no definitions.
static constexpr const char *kMesonNavigation = "";

static constexpr const char *kJamNavigation = R"scm(
(rule_definition) @definition
(actions_definition) @definition
)scm";

static constexpr const char *kComponentPascalNavigation = R"scm(
(module) @definition
(procedure_declaration) @definition
(type_declaration) @definition
)scm";

static constexpr const char *kPythonNavigation = R"scm(
(class_definition) @definition
(function_definition) @definition
)scm";

static constexpr const char *kMakeNavigation = R"scm(
(rule) @definition
(define_directive) @definition
)scm";

static constexpr const char *kCMakeNavigation = R"scm(
(function_def) @definition
(macro_def) @definition
)scm";


static constexpr const char *kCFolds = R"scm(
(compound_statement) @fold
(field_declaration_list) @fold
(enumerator_list) @fold
(initializer_list) @fold
(case_statement) @fold
(comment) @fold
(preproc_if) @fold
(preproc_ifdef) @fold
)scm";

static constexpr const char *kCppFolds = R"scm(
(compound_statement) @fold
(field_declaration_list) @fold
(enumerator_list) @fold
(initializer_list) @fold
(case_statement) @fold
(comment) @fold
(preproc_if) @fold
(preproc_ifdef) @fold
(declaration_list) @fold
)scm";

// An if statement folds with its elif and else clauses: the grammar has no
// node for the first branch alone.
static constexpr const char *kShellFolds = R"scm(
(compound_statement) @fold
(do_group) @fold
(if_statement) @fold
(elif_clause) @fold
(else_clause) @fold
(case_statement) @fold
(case_item) @fold
(subshell) @fold
(array) @fold
(heredoc_redirect) @fold
)scm";

static constexpr const char *kMesonFolds = R"scm(
(if_command) @fold
(elseif_command) @fold
(else_command) @fold
(foreach_command) @fold
(normal_command) @fold
(list) @fold
(dictionaries) @fold
)scm";

// Statements over several lines fold, like the long lists of Jamfiles.
static constexpr const char *kJamFolds = R"scm(
(block) @fold
(switch_statement) @fold
(case_clause) @fold
(actions_definition) @fold
(rule_invocation) @fold
(assignment) @fold
(on_assignment) @fold
(local_statement) @fold
)scm";

// Constructs closed by END keep the line of END visible.
static constexpr const char *kComponentPascalFolds = R"scm(
(procedure_declaration) @fold
(record_type) @fold
(if_statement) @fold
(case_statement) @fold
(while_statement) @fold
(repeat_statement) @fold
(for_statement) @fold
(loop_statement) @fold
(with_statement) @fold
(import_list) @fold
(const_section) @fold
(type_section) @fold
(var_section) @fold
(comment) @fold
)scm";

// "fold.end" marks a closing part that stays visible when it starts the last
// line.
static constexpr const char *kPythonFolds = R"scm(
(function_definition) @fold
(class_definition) @fold
(if_statement) @fold
(elif_clause) @fold
(else_clause) @fold
(for_statement) @fold
(while_statement) @fold
(try_statement) @fold
(except_clause) @fold
(finally_clause) @fold
(with_statement) @fold
(match_statement) @fold
(case_clause) @fold
(argument_list) @fold
(parameters) @fold
(list) @fold
(dictionary) @fold
(set) @fold
(tuple) @fold
(parenthesized_expression) @fold
(import_from_statement) @fold
(string (string_end) @fold.end) @fold
)scm";

static constexpr const char *kMakeFolds = R"scm(
(rule) @fold
(conditional) @fold
(define_directive) @fold
)scm";

static constexpr const char *kCMakeFolds = R"scm(
(if_condition (endif_command) @fold.end) @fold
(foreach_loop (endforeach_command) @fold.end) @fold
(while_loop (endwhile_command) @fold.end) @fold
(function_def (endfunction_command) @fold.end) @fold
(macro_def (endmacro_command) @fold.end) @fold
(block_def (endblock_command) @fold.end) @fold
(normal_command) @fold
(bracket_comment) @fold
(bracket_argument) @fold
)scm";


struct FileExtension {
	std::string_view extension;
	const char *language;
};

struct FileName {
	std::string_view name;
	const char *language;
};

// Headers are treated as C++, which is a superset for highlighting purposes.
static constexpr FileExtension kFileExtensions[] = {
	{"c",     "c"},
	{"cpp",   "cpp"},
	{"cc",    "cpp"},
	{"cxx",   "cpp"},
	{"c++",   "cpp"},
	{"h",     "cpp"},
	{"hh",    "cpp"},
	{"hpp",   "cpp"},
	{"hxx",   "cpp"},
	{"h++",   "cpp"},
	{"sh",    "shell"},
	{"bash",  "shell"},
	{"ksh",   "shell"},
	{"zsh",   "shell"},
	{"jam",   "jam"},
	{"cp",    "componentpascal"},
	{"py",    "python"},
	{"pyi",   "python"},
	{"mk",    "make"},
	{"mak",   "make"},
	{"cmake", "cmake"},
};

// Files known by their whole name, compared case-sensitively.
static constexpr FileName kFileNames[] = {
	{"meson.build",          "meson"},
	{"meson_options.txt",    "meson"},
	{"meson.options",        "meson"},
	{".profile",             "shell"},
	{".bashrc",              "shell"},
	{".bash_profile",        "shell"},
	{".bash_logout",         "shell"},
	{".zshrc",               "shell"},
	{".zprofile",            "shell"},
	{"profile",              "shell"},
	{"UserSetupEnvironment", "shell"},
	{"Jamfile",              "jam"},
	{"Jamrules",             "jam"},
	{"Jambase",              "jam"},
	{"Makefile",             "make"},
	{"makefile",             "make"},
	{"GNUmakefile",          "make"},
	{"makefile-engine",      "make"},
	{"CMakeLists.txt",       "cmake"},
};

// The Jam rules of the Haiku build have no extension.
static constexpr std::string_view kJamDirectory = "build/jam/";


Language::Language(
	const char *name,
	const TSLanguage *language,
	std::string_view highlights,
	std::string_view navigation,
	std::string_view folds
):
	fName(name),
	fLanguage(language),
	fHighlights(CompileQuery(language, highlights, "highlights")),
	fNavigation(CompileQuery(language, navigation, "navigation")),
	fFolds(CompileQuery(language, folds, "folds"))
{
	const uint32_t count = ts_query_capture_count(fHighlights);
	fCaptureStyles.reserve(count);
	for (uint32_t i = 0; i < count; i++) {
		uint32_t length = 0;
		const char *captureName = ts_query_capture_name_for_id(fHighlights, i, &length);
		fCaptureStyles.push_back(StyleForCapture(std::string_view(captureName, length)));
	}
	ParsePredicates();
}


void Language::ParsePredicates()
{
	const uint32_t patternCount = ts_query_pattern_count(fHighlights);
	fPatternPredicates.resize(patternCount);
	for (uint32_t pattern = 0; pattern < patternCount; pattern++) {
		uint32_t stepCount = 0;
		const TSQueryPredicateStep *steps = ts_query_predicates_for_pattern(fHighlights, pattern, &stepCount);
		auto stringValue = [this](const TSQueryPredicateStep &step) {
			uint32_t length = 0;
			const char *value = ts_query_string_value_for_id(fHighlights, step.value_id, &length);
			return std::string(value, length);
		};

		uint32_t start = 0;
		for (uint32_t i = 0; i < stepCount; i++) {
			if (steps[i].type != TSQueryPredicateStepTypeDone) {
				continue;
			}
			const TSQueryPredicateStep *predicate = steps + start;
			const uint32_t length = i - start;
			start = i + 1;

			Assert(length >= 3 && predicate[0].type == TSQueryPredicateStepTypeString
				&& predicate[1].type == TSQueryPredicateStepTypeCapture, "unsupported query predicate");
			const std::string name = stringValue(predicate[0]);
			Assert(name == "eq?" || name == "any-of?", "unsupported query predicate");
			Predicate entry;
			entry.captureId = predicate[1].value_id;
			for (uint32_t j = 2; j < length; j++) {
				Assert(predicate[j].type == TSQueryPredicateStepTypeString, "unsupported query predicate");
				entry.values.push_back(stringValue(predicate[j]));
			}
			fPatternPredicates[pattern].push_back(std::move(entry));
		}
	}
}


bool Language::MatchesPredicates(const TSQueryMatch &match, const Rope &text) const
{
	for (const Predicate &predicate : fPatternPredicates[match.pattern_index]) {
		for (uint16_t i = 0; i < match.capture_count; i++) {
			const TSQueryCapture &capture = match.captures[i];
			if (capture.index != predicate.captureId) {
				continue;
			}
			const uint32_t start = ts_node_start_byte(capture.node);
			const uint32_t end = std::min<uint32_t>(ts_node_end_byte(capture.node), text.Length());
			const std::string value = text.Substring(start, end > start ? end - start : 0);
			if (std::find(predicate.values.begin(), predicate.values.end(), value) == predicate.values.end()) {
				return false;
			}
		}
	}
	return true;
}


Language::~Language()
{
	ts_query_delete(fHighlights);
	ts_query_delete(fNavigation);
	ts_query_delete(fFolds);
}


// The built-in queries are part of the program; an error is a bug.
TSQuery *Language::CompileQuery(const TSLanguage *language, std::string_view source, const char *name)
{
	uint32_t errorOffset = 0;
	TSQueryError error = TSQueryErrorNone;
	TSQuery *query = ts_query_new(language, source.data(), source.size(), &errorOffset, &error);
	if (query == nullptr) {
		fprintf(stderr, "[CodeEditView] %s query error %d at offset %u: %.40s\n",
			name, static_cast<int>(error), errorOffset, source.data() + errorOffset);
	}
	Assert(query != nullptr, "invalid built-in query");
	return query;
}


const std::vector<const Language*> &Language::All()
{
	static const Language sC("c", tree_sitter_c(), kCHighlights, kCNavigation, kCFolds);
	static const Language sCpp("cpp", tree_sitter_cpp(), kCppHighlights, kCppNavigation, kCppFolds);
	static const Language sShell("shell", tree_sitter_bash(), kShellHighlights, kShellNavigation, kShellFolds);
	static const Language sMeson("meson", tree_sitter_meson(), kMesonHighlights, kMesonNavigation, kMesonFolds);
	static const Language sJam("jam", tree_sitter_jam(), kJamHighlights, kJamNavigation, kJamFolds);
	static const Language sComponentPascal("componentpascal", tree_sitter_componentpascal(),
		kComponentPascalHighlights, kComponentPascalNavigation, kComponentPascalFolds);
	static const Language sPython("python", tree_sitter_python(), kPythonHighlights, kPythonNavigation, kPythonFolds);
	static const Language sMake("make", tree_sitter_make(), kMakeHighlights, kMakeNavigation, kMakeFolds);
	static const Language sCMake("cmake", tree_sitter_cmake(), kCMakeHighlights, kCMakeNavigation, kCMakeFolds);
	static const std::vector<const Language*> sAll = {
		&sC, &sCpp, &sShell, &sMeson, &sJam, &sComponentPascal, &sPython, &sMake, &sCMake
	};
	return sAll;
}


const Language *Language::ForName(std::string_view name)
{
	for (const Language *language : All()) {
		if (name == language->Name()) {
			return language;
		}
	}
	return nullptr;
}


std::vector<std::string> AvailableLanguages()
{
	std::vector<std::string> names;
	for (const Language *language : Language::All()) {
		names.emplace_back(language->Name());
	}
	return names;
}


const char *LanguageForPath(std::string_view path)
{
	const size_t slash = path.rfind('/');
	const std::string_view leaf = slash == std::string_view::npos ? path : path.substr(slash + 1);
	for (const FileName &entry : kFileNames) {
		if (entry.name == leaf) {
			return entry.language;
		}
	}
	const size_t dot = leaf.rfind('.');
	if (dot != std::string_view::npos && dot != 0) {
		std::string extension(leaf.substr(dot + 1));
		std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
			return static_cast<char>(tolower(ch));
		});
		for (const FileExtension &entry : kFileExtensions) {
			if (entry.extension == extension) {
				return entry.language;
			}
		}
	}
	const std::string_view directory = path.substr(0, path.size() - leaf.size());
	const size_t jamDirectory = directory.find(kJamDirectory);
	if (jamDirectory != std::string_view::npos && (jamDirectory == 0 || directory[jamDirectory - 1] == '/')) {
		return "jam";
	}
	return nullptr;
}


} // CodeEdit
