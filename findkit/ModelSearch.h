#pragma once

#include <stddef.h>

#include <optional>
#include <string>
#include <string_view>

#include <CodeEditView/TextModel.h>

#include "Search.h"


namespace FindKit {


using CodeEdit::ObserverId;
using CodeEdit::Selection;
using CodeEdit::TextModel;


// The text of a model as searched; the rope is read a window at a time.
class RopeSource: public TextSource {
private:
	const CodeEdit::Rope &fText;

public:
	explicit RopeSource(const CodeEdit::Rope &text);

	size_t Length() const final {return fText.Length();}
	std::string_view View(size_t offset, size_t length, std::string &buffer) const final;
};


struct ReplaceAllResult {
	size_t count {};
	Selection selection;
};


// The match after the selection (backward: before it), continuing from the
// other end of the text if wrap is set. An empty match equal to an empty
// selection is skipped, so repeated searches advance.
std::optional<Match> FindNext(
	const Searcher &searcher,
	const TextSource &text,
	const Selection &selection,
	bool backward,
	bool wrap
);

// The match covering exactly the selection; an empty selection is never a
// match (replacing it would insert at the same place again and again).
std::optional<Match> SelectionMatch(const Searcher &searcher, const TextSource &text, const Selection &selection);


// The model functions lock the model. Edits are one undo step ("Replace",
// "Replace all") owned by owner (CodeEditView::EditOwner()), which restores
// the selection on undo.

// Replaces the selection if it is a match; returns the selection of the
// replacement text.
std::optional<Selection> ReplaceSelection(
	TextModel &model,
	ObserverId owner,
	const Searcher &searcher,
	std::string_view replacement,
	const Selection &selection
);

// Replaces all matches in the text, or with inSelection those lying entirely
// in the selection. The resulting selection covers the replaced range with
// inSelection, else it is a caret after the last replacement. Without matches
// the model is not changed and the selection is returned as is.
ReplaceAllResult ReplaceAll(
	TextModel &model,
	ObserverId owner,
	const Searcher &searcher,
	std::string_view replacement,
	const Selection &selection,
	bool inSelection
);


} // FindKit
