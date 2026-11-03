#pragma once

#include <atomic>

#include <Looper.h>

#include <tree_sitter/api.h>

#include <CodeEditView/TextModel.h>


namespace CodeEdit {


class Language;
class SyntaxState;


constexpr uint32 kMsgSyntaxParse = 'CEsp';


// Background thread that incrementally reparses a model's text from rope
// snapshots, without holding the model lock while parsing.
class SyntaxWorker: public BLooper {
private:
	TextModel &fModel;
	SyntaxState &fState;
	ObserverId fObserver;
	TSParser *fParser;
	TSTree *fTree = nullptr;
	bool fNeedsParse = true;
	std::atomic<bool> fQuitting {};

	void ApplyChanges(const ModelChanges &changes);
	void Parse();

public:
	// The model must be locked.
	SyntaxWorker(TextModel &model, SyntaxState &state, const Language &language);
	~SyntaxWorker() override;

	// The model must be locked. Afterwards the worker no longer accesses the
	// model or the state and deletes itself.
	void RequestQuit();

	void MessageReceived(BMessage *message) override;
};


} // CodeEdit
