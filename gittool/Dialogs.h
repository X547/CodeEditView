#pragma once

#include <string>
#include <vector>

#include <OS.h>
#include <Window.h>

#include "Plan.h"
#include "Snapshot.h"


class BCheckBox;
class BColumnListView;
class BButton;
class BMenuField;
class BTextControl;
class BTextView;


namespace GitTool {


// A window that the calling thread waits for, like BAlert::Go(). A calling
// window keeps drawing meanwhile.
class ModalDialog: public BWindow {
private:
	sem_id fSemaphore = -1;
	bool *fAccepted = nullptr;

protected:
	// Called in the dialog thread when the user accepts; false keeps the
	// dialog open.
	virtual bool Accept() {return true;}
	void Finish(bool accepted);

public:
	ModalDialog(const char *title, uint32 flags);

	// Shows the dialog and waits until it closes; true if accepted. The
	// dialog deletes itself.
	bool Go();

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


enum class FieldKind {
	Text,
	Password,
	MultiLine,
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


// Input fields shown in a modal dialog; the values are read after Go().
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

	// False if cancelled. The repository and snapshot serve the commit
	// picker.
	bool Go(git_repository *repository = nullptr, const Snapshot *snapshot = nullptr);
};


// Lets the user pick a commit from the history of a branch, remote copy,
// tag or worktree, or enter it. spec is the initial and the chosen value.
bool PickCommit(git_repository *repository, const Snapshot &snapshot, std::string &spec);
// The same window without choosing.
void ShowHistory(git_repository *repository, const Snapshot &snapshot, const std::string &spec);


// Shows the preview of a plan; true if the user chose to run it, with the
// resolution for overlapping worktree changes.
bool ConfirmPlan(const Plan &plan, git_repository *repository, Resolution &resolution);


struct ListColumn {
	std::string title;
	float width;
};


// Rows to choose from; selected receives the chosen row indices. With
// okLabel empty, the list is only shown.
bool ChooseFromList(
	const std::string &title,
	const std::string &okLabel,
	const std::vector<ListColumn> &columns,
	const std::vector<std::vector<std::string>> &rows,
	bool multiple,
	std::vector<int> &selected
);


void ShowText(const std::string &title, const std::string &text);

// Local date and time; empty for 0.
std::string FormatTime(int64_t time);


} // GitTool
