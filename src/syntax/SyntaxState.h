#pragma once

#include <OS.h>

#include <tree_sitter/api.h>


namespace CodeEdit {


class Language;
class SyntaxWorker;
class TextModel;
struct EditRecord;


TSInputEdit ToInputEdit(const EditRecord &edit);


// The syntax tree of a model, owned by the model. The published tree is
// edited synchronously with every text change, so its byte ranges always
// match the current text; a SyntaxWorker thread reparses in the background
// and publishes structurally updated trees.
//
// All methods require the model lock.
class SyntaxState {
private:
	TextModel &fModel;
	const Language &fLanguage;
	TSTree *fTree = nullptr;
	SyntaxWorker *fWorker;
	thread_id fWorkerThread;

public:
	SyntaxState(TextModel &model, const Language &language);
	~SyntaxState();

	SyntaxState(const SyntaxState &other) = delete;
	SyntaxState &operator=(const SyntaxState &other) = delete;

	const Language &GetLanguage() const {return fLanguage;}
	// Null until the first parse completes.
	const TSTree *Tree() const {return fTree;}

	void TextEdited(const EditRecord &edit);
	// Takes ownership of a tree consistent with the current text.
	void Publish(TSTree *tree);
};


} // CodeEdit
