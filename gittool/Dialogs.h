#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Messenger.h>
#include <OS.h>
#include <Window.h>

#include "Plan.h"
#include "Snapshot.h"


class BCheckBox;
class BColumnListView;
class BButton;
class BMenu;
class BMenuField;
class BPopUpMenu;
class BTextControl;
class BTextView;


namespace GitTool {


// A dialog opened without blocking (Open()), or, on a thread that is not a
// looper's, waited for (Go()).
class ModalDialog: public BWindow {
private:
	sem_id fSemaphore = -1;
	bool *fAccepted = nullptr;
	// Open(): the answer for the owner, until sent.
	BMessage fAnswer;
	BMessenger fAnswerTarget;
	bool fAnswerPending {};

	void SendAnswer(bool accepted);

protected:
	// Called in the dialog thread when the user accepts; false keeps the
	// dialog open.
	virtual bool Accept() {return true;}
	void Finish(bool accepted);

public:
	ModalDialog(const char *title, uint32 flags, window_feel feel = B_MODAL_APP_WINDOW_FEEL);

	// Shows the dialog modal to owner only and returns at once; done runs on
	// the owner's thread with whether it was accepted, unless the owner
	// closed first. Call on the owner's thread. The dialog deletes itself.
	void Open(BWindow *owner, std::function<void(bool accepted)> done);
	// Shows the dialog and waits until it closes; true if accepted. Only for
	// threads that are not loopers' (as network work); the dialog deletes
	// itself.
	bool Go();

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


enum class FieldKind {
	Text,
	Password,
	MultiLine,
	// A commit message, edited like in Git Diff.
	Message,
	Check,
	Choice,
	Commit,
	Label,
};


struct FormField {
	FieldKind kind = FieldKind::Text;
	std::string name;
	std::string label;
	std::string text;
	bool checked {};
	int choice {};
	std::vector<std::string> choices;
};


// Input fields shown in a modal dialog; the values are read after it is
// accepted.
class Form {
private:
	std::vector<FormField> fFields;

	const FormField &Field(const std::string &name) const;
	Form &Add(FieldKind kind, const std::string &name, const std::string &label);

public:
	std::string title;
	std::string okLabel;

	Form(const std::string &formTitle, const std::string &formOkLabel);

	Form &AddText(const std::string &name, const std::string &label, const std::string &text = std::string());
	Form &AddPassword(const std::string &name, const std::string &label);
	Form &AddMultiLine(const std::string &name, const std::string &label, const std::string &text = std::string());
	Form &AddMessage(const std::string &name, const std::string &label, const std::string &text = std::string());
	Form &AddCheck(const std::string &name, const std::string &label, bool checked = false);
	Form &AddChoice(const std::string &name, const std::string &label, const std::vector<std::string> &choices,
		int choice = 0);
	// A commit in revision syntax, with a button that opens the commit
	// picker.
	Form &AddCommit(const std::string &name, const std::string &label, const std::string &spec = std::string());
	Form &AddLabel(const std::string &text);

	std::vector<FormField> &Fields() {return fFields;}
	std::string Text(const std::string &name) const {return Field(name).text;}
	bool Checked(const std::string &name) const {return Field(name).checked;}
	int Choice(const std::string &name) const {return Field(name).choice;}

	// Opens the form modal to owner and returns at once; accepted runs on the
	// owner's thread with the values, cancelled (if any) when the form is
	// cancelled. The repository and snapshot serve the commit picker; the
	// dialog uses its own handle and copy.
	void Open(BWindow *owner, std::function<void(const Form &form)> accepted,
		git_repository *repository = nullptr, const Snapshot *snapshot = nullptr,
		std::function<void()> cancelled = nullptr) const;
	// False if cancelled. Only for threads that are not loopers'.
	bool Go(git_repository *repository = nullptr, const Snapshot *snapshot = nullptr);
};


// Lets the user pick a commit from the history of a branch, remote copy,
// tag or worktree, or enter it, in a dialog modal to owner; chosen runs on
// the owner's thread with the commit in revision syntax. The picker uses its
// own repository handle and snapshot copy.
void PickCommit(BWindow *owner, git_repository *repository, const Snapshot &snapshot, const std::string &spec,
	std::function<void(const std::string &spec)> chosen);
// The branches, remote copies, worktree positions and recent tags; items
// send kMsgHistoryOf with "spec", the one equal to current is marked.
BPopUpMenu *RefMenu(const Snapshot &snapshot, const std::string &current);
// Replaces the items of a menu by those of RefMenu.
void FillRefMenu(BMenu *menu, const Snapshot &snapshot, const std::string &current);


// Shows the preview of a plan modal to owner; run runs on the owner's thread
// if the user chose to run it, with the resolution for overlapping worktree
// changes.
void ConfirmPlan(BWindow *owner, const Plan &plan, git_repository *repository,
	std::function<void(Resolution resolution)> run);

// Runs a plan that needs no network on a thread with its own handle of the
// repository, under a progress dialog over owner; then done runs on the
// owner's thread with the error (empty on success).
void RunPlanWithProgress(
	BWindow *owner,
	const std::string &repositoryPath,
	const Plan &plan,
	Resolution resolution,
	std::function<void(const std::string &error)> done
);


struct ListColumn {
	std::string title;
	float width;
};


// Rows to choose from, modal to owner; chosen runs on the owner's thread
// with the chosen row indices. With okLabel empty, the list is only shown.
void ChooseFromList(
	BWindow *owner,
	const std::string &title,
	const std::string &okLabel,
	const std::vector<ListColumn> &columns,
	const std::vector<std::vector<std::string>> &rows,
	bool multiple,
	std::function<void(const std::vector<int> &selected)> chosen
);


// A text to read, modal to owner.
void ShowText(BWindow *owner, const std::string &title, const std::string &text);

// Local date and time; empty for 0.
std::string FormatTime(int64_t time);


} // GitTool
