#pragma once

#include <stdint.h>

#include <string>
#include <string_view>
#include <vector>

#include <CodeEditView/EditorSettings.h>
#include <CodeEditView/ViewTypes.h>


namespace GitDiff {


// Git's convention for commit messages: a short subject line, a blank line,
// and a body wrapped at 72 columns.
constexpr uint32_t kSubjectLimit = 50;
constexpr uint32_t kBodyLimit = 72;


// Editor settings for writing a commit message: a guide line at the body
// limit, and the body (not the subject) wrapped there while typing. Long
// lines also wrap at the view width.
CodeEdit::EditorSettings CommitMessageSettings();

// Highlights what breaks the convention: the subject past its limit, text
// on the second line, and body lines past their limit.
std::vector<CodeEdit::Decoration> CommitMessageDecorations(std::string_view text);

// Adds Gerrit's "Change-Id: <changeId>" trailer to a cleaned-up message, as
// Gerrit's commit-msg hook does: before any Signed-off-by line of the
// trailer block, or in a new paragraph. The message is unchanged if its
// trailers already have a Change-Id, or if it is a "fixup!" or "squash!"
// commit, which is folded into another one.
std::string AddChangeId(std::string_view message, std::string_view changeId);


} // GitDiff
