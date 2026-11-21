#pragma once

#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include <tree_sitter/api.h>

#include <CodeEditView/Theme.h>


namespace CodeEdit {


class Rope;


// A built-in language with its compiled queries. Instances are created once
// and never destroyed; queries are immutable and shared between threads.
class Language {
private:
	const char *fName;
	const TSLanguage *fLanguage;
	TSQuery *fHighlights;
	TSQuery *fNavigation;
	TSQuery *fFolds;
	std::vector<HighlightStyle> fCaptureStyles;

	// Text predicates of a highlight pattern: the captured text must be one
	// of the values (#eq? and #any-of?) or match the pattern (#match?).
	struct Predicate {
		uint32_t captureId {};
		std::vector<std::string> values;
		std::optional<std::regex> pattern;
	};
	std::vector<std::vector<Predicate>> fPatternPredicates;

	static TSQuery *CompileQuery(const TSLanguage *language, std::string_view source, const char *name);
	void ParsePredicates();

public:
	Language(
		const char *name,
		const TSLanguage *language,
		std::string_view highlights,
		std::string_view navigation,
		std::string_view folds
	);
	~Language();

	Language(const Language &other) = delete;
	Language &operator=(const Language &other) = delete;

	static const Language *ForName(std::string_view name);
	static const std::vector<const Language*> &All();

	const char *Name() const {return fName;}
	const TSLanguage *Grammar() const {return fLanguage;}
	const TSQuery *Highlights() const {return fHighlights;}
	// Captures "definition" nodes for previous/next definition navigation.
	const TSQuery *Navigation() const {return fNavigation;}
	// Captures "fold" nodes: candidate ranges for folding.
	const TSQuery *Folds() const {return fFolds;}
	HighlightStyle CaptureStyle(uint32_t captureIndex) const {return fCaptureStyles[captureIndex];}
	bool MatchesPredicates(const TSQueryMatch &match, const Rope &text) const;
};


} // CodeEdit
