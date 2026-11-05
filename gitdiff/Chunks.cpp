#include "Chunks.h"

#include <algorithm>
#include <functional>
#include <string>

#include <CodeEditView/TextModel.h>

#include "Git.h"


namespace GitDiff {


using CodeEdit::ModelLock;
using CodeEdit::Rope;
using CodeEdit::TextModel;


struct LinePiece {
	// Without the newline.
	std::string text;
	bool newline {};
};


static int AddHunk(const git_diff_delta *delta, const git_diff_hunk *hunk, void *payload)
{
	(void)delta;
	// A side without lines reports the line after which the other side's
	// lines go (1-based), which is the 0-based line before which they go.
	Chunk chunk;
	chunk.aCount = hunk->old_lines;
	chunk.aFirst = hunk->old_lines == 0 ? hunk->old_start : hunk->old_start - 1;
	chunk.bCount = hunk->new_lines;
	chunk.bFirst = hunk->new_lines == 0 ? hunk->new_start : hunk->new_start - 1;
	static_cast<std::vector<Chunk>*>(payload)->push_back(chunk);
	return 0;
}


std::vector<Chunk> ComputeChunks(std::string_view a, std::string_view b, DiffAlgorithm algorithm)
{
	git_diff_options options = GIT_DIFF_OPTIONS_INIT;
	options.context_lines = 0;
	options.interhunk_lines = 0;
	options.flags = GIT_DIFF_FORCE_TEXT;
	switch (algorithm) {
		case DiffAlgorithm::Minimal:
			options.flags |= GIT_DIFF_MINIMAL;
			break;
		case DiffAlgorithm::Patience:
			options.flags |= GIT_DIFF_PATIENCE;
			break;
		default:
			break;
	}
	// Empty buffers would be taken as missing files.
	static const char kEmpty[] = "";
	std::vector<Chunk> chunks;
	Check(git_diff_buffers(
		a.empty() ? kEmpty : a.data(), a.size(), nullptr,
		b.empty() ? kEmpty : b.data(), b.size(), nullptr,
		&options, nullptr, nullptr, AddHunk, nullptr, &chunks
	), "Cannot compare the texts");
	return chunks;
}


double MapLine(const std::vector<Chunk> &chunks, double line, bool fromA)
{
	double delta = 0;
	for (const Chunk &chunk : chunks) {
		const double from = static_cast<double>(fromA ? chunk.aFirst : chunk.bFirst);
		const double fromCount = static_cast<double>(fromA ? chunk.aCount : chunk.bCount);
		const double to = static_cast<double>(fromA ? chunk.bFirst : chunk.aFirst);
		const double toCount = static_cast<double>(fromA ? chunk.bCount : chunk.aCount);
		if (line < from) {
			return line + delta;
		}
		if (line < from + fromCount) {
			return to + (line - from) * toCount / fromCount;
		}
		delta = (to + toCount) - (from + fromCount);
	}
	return line + delta;
}


std::vector<size_t> TouchedChunks(const std::vector<Chunk> &chunks, const LineRange &range, bool onA)
{
	std::vector<size_t> indexes;
	for (size_t i = 0; i < chunks.size(); i++) {
		const size_t first = onA ? chunks[i].aFirst : chunks[i].bFirst;
		const size_t count = onA ? chunks[i].aCount : chunks[i].bCount;
		const bool touched = count > 0
			? first <= range.last && first + count - 1 >= range.first
			: range.first <= first && first <= range.last + 1;
		if (touched) {
			indexes.push_back(i);
		}
	}
	return indexes;
}


// Start of a line, or the end of the text past the last line.
static size_t LineOffset(const Rope &text, size_t line)
{
	return line <= text.NewlineCount() ? text.LineStart(line) : text.Length();
}


static LinePiece LineAt(const Rope &text, size_t line)
{
	const size_t start = text.LineStart(line);
	return {text.Substring(start, text.LineEnd(line) - start), line < text.NewlineCount()};
}


// Lines ending with a newline, except the last one at the end of the text if
// it had none.
static std::string JoinLines(const std::vector<LinePiece> &lines, bool textFollows)
{
	std::string result;
	for (size_t i = 0; i < lines.size(); i++) {
		result += lines[i].text;
		if (i + 1 < lines.size() || lines[i].newline || textFollows) {
			result += '\n';
		}
	}
	return result;
}


static bool IsSelected(const LineRange *selection, size_t line)
{
	return selection == nullptr || (line >= selection->first && line <= selection->last);
}


// Later chunks first, so the line numbers of earlier ones stay valid.
static std::vector<size_t> Descending(std::vector<size_t> indexes)
{
	std::sort(indexes.begin(), indexes.end(), std::greater<size_t>());
	indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
	return indexes;
}


static void ReplaceLines(TextModel &model, size_t first, size_t count, const std::vector<LinePiece> &lines)
{
	const Rope &text = model.Text();
	const size_t start = LineOffset(text, first);
	const size_t end = LineOffset(text, first + count);
	model.Replace(start, end - start, JoinLines(lines, end < text.Length()));
}


void ApplyToA(TextModel &a, TextModel &b, const std::vector<Chunk> &chunks,
	const std::vector<size_t> &indexes, const LineRange *selection)
{
	Rope bText;
	{
		ModelLock lock(b);
		bText = b.Text();
	}
	ModelLock lock(a);
	a.BeginGroup(CodeEdit::kNoObserver, CodeEdit::EditKind::Other, {});
	for (const size_t index : Descending(indexes)) {
		const Chunk &chunk = chunks.at(index);
		std::vector<LinePiece> lines;
		for (size_t line = chunk.bFirst; line < chunk.bFirst + chunk.bCount; line++) {
			if (IsSelected(selection, line)) {
				lines.push_back(LineAt(bText, line));
			}
		}
		ReplaceLines(a, chunk.aFirst, chunk.aCount, lines);
	}
	a.EndGroup({});
}


void RevertB(TextModel &a, TextModel &b, const std::vector<Chunk> &chunks,
	const std::vector<size_t> &indexes, const LineRange *selection)
{
	Rope aText;
	{
		ModelLock lock(a);
		aText = a.Text();
	}
	ModelLock lock(b);
	b.BeginGroup(CodeEdit::kNoObserver, CodeEdit::EditKind::Other, {});
	for (const size_t index : Descending(indexes)) {
		const Chunk &chunk = chunks.at(index);
		std::vector<LinePiece> lines;
		for (size_t line = chunk.aFirst; line < chunk.aFirst + chunk.aCount; line++) {
			lines.push_back(LineAt(aText, line));
		}
		if (selection != nullptr) {
			for (size_t line = chunk.bFirst; line < chunk.bFirst + chunk.bCount; line++) {
				if (!IsSelected(selection, line)) {
					lines.push_back(LineAt(b.Text(), line));
				}
			}
		}
		ReplaceLines(b, chunk.bFirst, chunk.bCount, lines);
	}
	b.EndGroup({});
}


} // GitDiff
