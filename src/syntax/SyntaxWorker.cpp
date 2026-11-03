#include "SyntaxWorker.h"

#include <string_view>

#include "Language.h"
#include "SyntaxState.h"


namespace CodeEdit {


static const char *ReadRope(void *payload, uint32_t byteIndex, TSPoint position, uint32_t *bytesRead)
{
	(void)position;
	const Rope &rope = *static_cast<const Rope*>(payload);
	if (byteIndex >= rope.Length()) {
		*bytesRead = 0;
		return "";
	}
	const std::string_view chunk = rope.ChunkAt(byteIndex);
	*bytesRead = static_cast<uint32_t>(chunk.size());
	return chunk.data();
}


SyntaxWorker::SyntaxWorker(TextModel &model, SyntaxState &state, const Language &language):
	BLooper("syntax worker", B_LOW_PRIORITY),
	fModel(model),
	fState(state),
	fParser(ts_parser_new())
{
	ts_parser_set_language(fParser, language.Grammar());
	fObserver = fModel.AddObserver(BMessenger(this));
}


SyntaxWorker::~SyntaxWorker()
{
	if (fTree != nullptr) {
		ts_tree_delete(fTree);
	}
	ts_parser_delete(fParser);
}


void SyntaxWorker::RequestQuit()
{
	fQuitting = true;
	fModel.RemoveObserver(fObserver);
	PostMessage(B_QUIT_REQUESTED);
}


// The model must be locked.
void SyntaxWorker::ApplyChanges(const ModelChanges &changes)
{
	if (changes.reset) {
		if (fTree != nullptr) {
			ts_tree_delete(fTree);
			fTree = nullptr;
		}
		fNeedsParse = true;
		return;
	}
	for (const EditRecord &edit : changes.edits) {
		if (fTree != nullptr) {
			const TSInputEdit inputEdit = ToInputEdit(edit);
			ts_tree_edit(fTree, &inputEdit);
		}
		fNeedsParse = true;
	}
}


// Parses a snapshot of the text outside of the model lock. Edits made while
// parsing are applied to the new tree before it is published, so the
// published tree always matches the text; another parse follows them.
void SyntaxWorker::Parse()
{
	Rope snapshot;
	{
		ModelLock lock(fModel);
		if (fQuitting) {
			return;
		}
		ApplyChanges(fModel.Sync(fObserver));
		if (!fNeedsParse) {
			return;
		}
		snapshot = fModel.Text();
	}

	const TSInput input {&snapshot, ReadRope, TSInputEncodingUTF8, nullptr};
	TSTree *tree = ts_parser_parse(fParser, fTree, input);
	if (tree == nullptr) {
		return;
	}

	ModelLock lock(fModel);
	if (fQuitting) {
		ts_tree_delete(tree);
		return;
	}
	const ModelChanges changes = fModel.Sync(fObserver);
	if (changes.reset) {
		ts_tree_delete(tree);
		ApplyChanges(changes);
		PostMessage(kMsgSyntaxParse);
		return;
	}
	for (const EditRecord &edit : changes.edits) {
		const TSInputEdit inputEdit = ToInputEdit(edit);
		ts_tree_edit(tree, &inputEdit);
	}
	if (fTree != nullptr) {
		ts_tree_delete(fTree);
	}
	fTree = tree;
	fNeedsParse = !changes.edits.empty();
	fState.Publish(ts_tree_copy(tree));
	if (fNeedsParse) {
		PostMessage(kMsgSyntaxParse);
	}
}


void SyntaxWorker::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgModelChanged:
		case kMsgSyntaxParse:
			Parse();
			break;
		default:
			BLooper::MessageReceived(message);
			break;
	}
}


} // CodeEdit
