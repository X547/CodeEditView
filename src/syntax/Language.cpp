#include "Language.h"

#include <ctype.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include <CodeEditView/Language.h>
#include <CodeEditView/Rope.h>

#include "support/Assert.h"

#include "CHighlights.inc"
#include "CppHighlights.inc"


extern "C" const TSLanguage *tree_sitter_c();
extern "C" const TSLanguage *tree_sitter_cpp();


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


struct FileExtension {
	std::string_view extension;
	const char *language;
};

// Headers are treated as C++, which is a superset for highlighting purposes.
static constexpr FileExtension kFileExtensions[] = {
	{"c",   "c"},
	{"cpp", "cpp"},
	{"cc",  "cpp"},
	{"cxx", "cpp"},
	{"c++", "cpp"},
	{"h",   "cpp"},
	{"hh",  "cpp"},
	{"hpp", "cpp"},
	{"hxx", "cpp"},
	{"h++", "cpp"},
};


Language::Language(const char *name, const TSLanguage *language, std::string_view highlights, std::string_view navigation):
	fName(name),
	fLanguage(language),
	fHighlights(CompileQuery(language, highlights, "highlights")),
	fNavigation(CompileQuery(language, navigation, "navigation"))
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
	static const Language sC("c", tree_sitter_c(), kCHighlights, kCNavigation);
	static const Language sCpp("cpp", tree_sitter_cpp(), kCppHighlights, kCppNavigation);
	static const std::vector<const Language*> sAll = {&sC, &sCpp};
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
	const size_t dot = leaf.rfind('.');
	if (dot == std::string_view::npos || dot == 0) {
		return nullptr;
	}
	std::string extension(leaf.substr(dot + 1));
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
		return static_cast<char>(tolower(ch));
	});
	for (const FileExtension &entry : kFileExtensions) {
		if (entry.extension == extension) {
			return entry.language;
		}
	}
	return nullptr;
}


} // CodeEdit
