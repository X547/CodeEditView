#pragma once

#include <string>


class BScrollView;
class CodeEditView;


namespace GitDiff {


// A CodeEditView set up for a commit message (CommitMessageSettings), with
// its own model. Its target gets kMsgContentsChanged; call
// UpdateMessageDecorations then.
CodeEditView *CreateMessageEditor(const char *name);

// The editor in a scroll view, without line numbers: at least 6 lines high
// (split views start items at their minimum), rows lines preferred.
BScrollView *MessageScrollView(const char *name, CodeEditView *editor, int rows);

std::string MessageText(CodeEditView *editor);
void SetMessageText(CodeEditView *editor, const std::string &text);
// Highlights the parts that break the commit message convention.
void UpdateMessageDecorations(CodeEditView *editor);


} // GitDiff
