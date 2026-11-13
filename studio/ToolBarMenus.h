#pragma once

#include <stddef.h>

#include <atomic>
#include <string>
#include <vector>

#include <Locker.h>
#include <Message.h>
#include <Messenger.h>
#include <Referenceable.h>

#include <CodeEditView/TextModel.h>

#include "LazyMenu.h"


namespace Studio {


// Menus of the tool bar, made when opened. They run in the menu's thread
// and use the model only under its lock.

// A menu on the text of a model, which may be exchanged.
class ModelMenu: public LazyMenu {
private:
	mutable BLocker fLock;
	BReference<CodeEdit::TextModel> fModel;

protected:
	BReference<CodeEdit::TextModel> Model() const;

public:
	ModelMenu(const char *name, CodeEdit::TextModel *model);

	void SetModel(CodeEdit::TextModel *model);
};


// The definitions of the text in a flat list, members indented; sections
// ("#pragma mark") are separators with their name, as in Pe. The innermost
// definition holding the caret is checked, as the outline selects it. An
// item sends a copy of the message with "start" and "end" (uint64): the name
// of the definition.
class DefinitionsMenu: public ModelMenu {
private:
	enum class EntryKind {
		Definition,
		Section,
		Separator,
		Nothing,
	};

	struct Entry {
		EntryKind kind {};
		std::string label;
		size_t start {};
		size_t end {};
		size_t nameStart {};
		size_t nameEnd {};
	};

	BMessage fMessage;
	BMessenger fTarget;
	std::atomic<size_t> fCaret {};
	std::vector<Entry> fEntries;
	// The entry to check; fEntries.size() for none.
	size_t fCaretEntry {};

	void AddSeparator(const std::string &label);
	void AddEntries(const std::vector<CodeEdit::OutlineItem> &items, int32 depth);

protected:
	size_t Prepare() final;
	BMenuItem *MakeItem(size_t index) final;
	void Release() final;

public:
	DefinitionsMenu(const char *name, CodeEdit::TextModel *model, const BMessage &message,
		const BMessenger &target);

	// May be called from any thread.
	void SetCaret(size_t caret) {fCaret = caret;}
};


struct Include {
	std::string name;
	// Written in angle brackets.
	bool system {};
};


// The files named by "#include" directives, in order, each once.
std::vector<Include> FindIncludes(const CodeEdit::Rope &text);


// The files included by the text. An item sends a copy of the message with
// "name" (string) and "system" (bool).
class IncludesMenu: public ModelMenu {
private:
	BMessage fMessage;
	BMessenger fTarget;
	std::vector<Include> fIncludes;

protected:
	size_t Prepare() final;
	BMenuItem *MakeItem(size_t index) final;
	void Release() final;

public:
	IncludesMenu(const char *name, CodeEdit::TextModel *model, const BMessage &message, const BMessenger &target);
};


// The open commands and the files of the application opened last.
class OpenMenu: public LazyMenu {
private:
	BMessenger fTarget;
	std::string fSignature;
	int32 fRecentCount;
	std::vector<BMenuItem*> fItems;

protected:
	size_t Prepare() final;
	BMenuItem *MakeItem(size_t index) final;
	void Release() final;

public:
	OpenMenu(const char *name, const BMessenger &target, const char *signature, int32 recentCount);
	~OpenMenu() override;
};


} // Studio
