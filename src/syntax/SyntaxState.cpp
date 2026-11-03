#include "SyntaxState.h"

#include <stdlib.h>

#include <CodeEditView/EditRecord.h>
#include <CodeEditView/TextModel.h>

#include "SyntaxWorker.h"


namespace CodeEdit {


static TSPoint ToPoint(const TextPoint &point)
{
	return {static_cast<uint32_t>(point.line), static_cast<uint32_t>(point.column)};
}


TSInputEdit ToInputEdit(const EditRecord &edit)
{
	return {
		static_cast<uint32_t>(edit.offset),
		static_cast<uint32_t>(edit.OldEnd()),
		static_cast<uint32_t>(edit.NewEnd()),
		ToPoint(edit.start),
		ToPoint(edit.oldEnd),
		ToPoint(edit.newEnd)
	};
}


SyntaxState::SyntaxState(TextModel &model, const Language &language):
	fModel(model),
	fLanguage(language),
	fWorker(new SyntaxWorker(model, *this, language))
{
	fWorkerThread = fWorker->Run();
	fWorker->PostMessage(kMsgSyntaxParse);
}


// The worker may be blocked on the model lock held by the caller, so it is
// only told to quit here; the model waits for the thread when it is destroyed.
SyntaxState::~SyntaxState()
{
	fWorker->RequestQuit();
	fModel.RetireWorkerThread(fWorkerThread);
	if (fTree != nullptr) {
		ts_tree_delete(fTree);
	}
}


void SyntaxState::TextEdited(const EditRecord &edit)
{
	if (fTree == nullptr) {
		return;
	}
	const TSInputEdit inputEdit = ToInputEdit(edit);
	ts_tree_edit(fTree, &inputEdit);
}


void SyntaxState::Publish(TSTree *tree)
{
	bool changed = true;
	if (fTree != nullptr) {
		uint32_t count = 0;
		TSRange *ranges = ts_tree_get_changed_ranges(fTree, tree, &count);
		free(ranges);
		changed = count > 0;
		ts_tree_delete(fTree);
	}
	fTree = tree;
	if (changed) {
		fModel.SyntaxChanged();
	}
}


} // CodeEdit
