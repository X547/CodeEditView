#include "MessageEditor.h"

#include <math.h>

#include <Font.h>
#include <ScrollView.h>

#include <CodeEditView/CodeEditView.h>
#include <CodeEditView/TextModel.h>

#include "CommitMessage.h"


namespace GitDiff {


CodeEditView *CreateMessageEditor(const char *name)
{
	CodeEditView *editor = new CodeEditView(name);
	editor->SetSettings(CommitMessageSettings());
	return editor;
}


BScrollView *MessageScrollView(const char *name, CodeEditView *editor, int rows)
{
	font_height height;
	be_fixed_font->GetHeight(&height);
	const float lineHeight = ceilf(height.ascent + height.descent + height.leading);
	const float cellWidth = be_fixed_font->StringWidth("M");
	editor->SetExplicitMinSize(BSize(cellWidth * 10, lineHeight * 6));
	editor->SetExplicitPreferredSize(BSize(cellWidth * (kBodyLimit + 2), lineHeight * rows));
	return new BScrollView(name, editor, 0, false, true);
}


std::string MessageText(CodeEditView *editor)
{
	CodeEdit::TextModel &model = *editor->Model();
	CodeEdit::ModelLock lock(model);
	return model.Text().ToString();
}


void SetMessageText(CodeEditView *editor, const std::string &text)
{
	{
		CodeEdit::TextModel &model = *editor->Model();
		CodeEdit::ModelLock lock(model);
		model.SetText(text);
	}
	UpdateMessageDecorations(editor);
}


void UpdateMessageDecorations(CodeEditView *editor)
{
	editor->SetDecorations(CommitMessageDecorations(MessageText(editor)));
}


} // GitDiff
