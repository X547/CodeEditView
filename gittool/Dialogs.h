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
class Dialog: public BWindow {
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
	Dialog(const char *title, uint32 flags);

	// Shows the dialog over owner and returns at once; done runs on the
	// owner's thread with whether it was accepted, unless the owner closed
	// first. The owner goes on taking input. Call on the owner's thread. The
	// dialog deletes itself.
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
	// A text field that takes People files: their e-mail addresses are
	// added, separated by commas.
	bool acceptsPeople {};
};


// Input fields shown in a dialog; the values are read after it is
// accepted.
class Form {
public:
	// A text field that follows another one (Derive()).
	struct Derivation {
		std::string target;
		std::string source;
		std::function<std::string(const std::string &sourceText)> derive;
	};
	// Runs with the values each time the user accepts (OpenWithAction());
	// finished closes the form with an empty error, else shows the error.
	using Action = std::function<void(const Form &form, std::function<void(const std::string &error)> finished)>;

private:
	std::vector<FormField> fFields;
	std::vector<Derivation> fDerivations;

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
	// The text field takes dropped People files (FormField::acceptsPeople).
	Form &AcceptPeople(const std::string &name);
	// The text field target is derive(text of source) as source changes,
	// until the user edits target.
	Form &Derive(const std::string &target, const std::string &source,
		std::function<std::string(const std::string &sourceText)> derive);

	std::vector<FormField> &Fields() {return fFields;}
	const std::vector<Derivation> &Derivations() const {return fDerivations;}
	std::string Text(const std::string &name) const {return Field(name).text;}
	bool Checked(const std::string &name) const {return Field(name).checked;}
	int Choice(const std::string &name) const {return Field(name).choice;}

	// Opens the form over owner and returns at once; accepted runs on the
	// owner's thread with the values, cancelled (if any) when the form is
	// cancelled. The repository and snapshot serve the commit picker; the
	// dialog uses its own handle and copy.
	void Open(BWindow *owner, std::function<void(const Form &form)> accepted,
		git_repository *repository = nullptr, const Snapshot *snapshot = nullptr,
		std::function<void()> cancelled = nullptr) const;
	// Opens the form over owner; each time the user accepts, action runs
	// on the owner's thread with the values while the form stays open with
	// its fields disabled. Its outcome closes the form, or shows the error
	// in it so that the user can correct the values and try again.
	void OpenWithAction(BWindow *owner, Action action) const;
	// False if cancelled. Only for threads that are not loopers'.
	bool Go(git_repository *repository = nullptr, const Snapshot *snapshot = nullptr);
};


// Lets the user pick a commit from the history of a branch, remote copy,
// tag or worktree, or enter it, in a dialog over owner; chosen runs on
// the owner's thread with the commit in revision syntax. The picker uses its
// own repository handle and snapshot copy.
void PickCommit(BWindow *owner, git_repository *repository, const Snapshot &snapshot, const std::string &spec,
	std::function<void(const std::string &spec)> chosen);
// The branches, remote copies, worktree positions and recent tags; items
// send kMsgHistoryOf with "spec", the one equal to current is marked.
BPopUpMenu *RefMenu(const Snapshot &snapshot, const std::string &current);
// Replaces the items of a menu by those of RefMenu.
void FillRefMenu(BMenu *menu, const Snapshot &snapshot, const std::string &current);


// Shows the preview of a plan over owner; run runs on the owner's thread
// if the user chose to run it, with the resolution for overlapping worktree
// changes. A plan that needs no preview (RunsWithoutPreview()) runs at once.
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


// Rows to choose from, in a dialog over owner; chosen runs on the owner's thread
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


// A text to read, in a dialog over owner.
void ShowText(BWindow *owner, const std::string &title, const std::string &text);

// Local date and time; empty for 0.
std::string FormatTime(int64_t time);


} // GitTool
