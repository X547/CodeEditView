#include "Dialogs.h"

#include <time.h>

#include <algorithm>
#include <stdexcept>

#include <Button.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <GridLayout.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include "CommitWindow.h"
#include "DiffView.h"
#include "Messages.h"


namespace GitTool {


//#pragma mark - ModalDialog

ModalDialog::ModalDialog(const char *title, uint32 flags, window_feel feel):
	BWindow(
		BRect(0, 0, 100, 100),
		title,
		B_TITLED_WINDOW_LOOK,
		feel,
		flags | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE
	)
{
}


bool ModalDialog::Go()
{
	bool accepted = false;
	fAccepted = &accepted;
	const sem_id semaphore = create_sem(0, "modal dialog");
	fSemaphore = semaphore;
	BWindow *parent = dynamic_cast<BWindow*>(BLooper::LooperForThread(find_thread(nullptr)));
	ResizeToPreferred();
	if (parent != nullptr) {
		CenterIn(parent->Frame());
	} else {
		CenterOnScreen();
	}
	Show();
	if (parent != nullptr) {
		// Keeps the calling window drawn while it waits.
		while (true) {
			const status_t result = acquire_sem_etc(semaphore, 1, B_RELATIVE_TIMEOUT, 50000);
			if (result == B_OK || result == B_BAD_SEM_ID) {
				break;
			}
			parent->UpdateIfNeeded();
		}
	} else {
		while (acquire_sem(semaphore) == B_INTERRUPTED) {
		}
	}
	delete_sem(semaphore);
	return accepted;
}


void ModalDialog::Finish(bool accepted)
{
	if (fAccepted != nullptr) {
		*fAccepted = accepted;
		fAccepted = nullptr;
		release_sem(fSemaphore);
	}
	Quit();
}


bool ModalDialog::QuitRequested()
{
	if (fAccepted != nullptr) {
		*fAccepted = false;
		fAccepted = nullptr;
		release_sem(fSemaphore);
	}
	return true;
}


void ModalDialog::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgDialogOk:
			if (Accept()) {
				Finish(true);
			}
			break;
		case kMsgDialogCancel:
			Finish(false);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


static BGroupLayout *ButtonRow(BButton *cancel, BButton *ok)
{
	BGroupLayout *row = BLayoutBuilder::Group<>(B_HORIZONTAL)
		.AddGlue()
		.Add(cancel);
	if (ok != nullptr) {
		row->AddView(ok);
	}
	return row;
}


static BScrollView *ReadOnlyText(BTextView *&textView, const std::string &text, float width, float height)
{
	textView = new BTextView("text");
	textView->SetText(text.c_str());
	textView->MakeEditable(false);
	textView->SetWordWrap(true);
	BScrollView *scrollView = new BScrollView("textScroll", textView, 0, false, true);
	scrollView->SetExplicitPreferredSize(BSize(width, height));
	return scrollView;
}


//#pragma mark - Form

Form::Form(const std::string &formTitle, const std::string &formOkLabel):
	title(formTitle),
	okLabel(formOkLabel)
{
}


const FormField &Form::Field(const std::string &name) const
{
	for (const FormField &field : fFields) {
		if (field.name == name) {
			return field;
		}
	}
	throw std::logic_error("No form field " + name);
}


Form &Form::Add(FieldKind kind, const std::string &name, const std::string &label)
{
	FormField field;
	field.kind = kind;
	field.name = name;
	field.label = label;
	fFields.push_back(std::move(field));
	return *this;
}


Form &Form::AddText(const std::string &name, const std::string &label, const std::string &text)
{
	Add(FieldKind::Text, name, label);
	fFields.back().text = text;
	return *this;
}


Form &Form::AddPassword(const std::string &name, const std::string &label)
{
	return Add(FieldKind::Password, name, label);
}


Form &Form::AddMultiLine(const std::string &name, const std::string &label, const std::string &text)
{
	Add(FieldKind::MultiLine, name, label);
	fFields.back().text = text;
	return *this;
}


Form &Form::AddCheck(const std::string &name, const std::string &label, bool checked)
{
	Add(FieldKind::Check, name, label);
	fFields.back().checked = checked;
	return *this;
}


Form &Form::AddChoice(const std::string &name, const std::string &label, const std::vector<std::string> &choices,
	int choice)
{
	Add(FieldKind::Choice, name, label);
	fFields.back().choices = choices;
	fFields.back().choice = choice;
	return *this;
}


Form &Form::AddCommit(const std::string &name, const std::string &label, const std::string &spec)
{
	Add(FieldKind::Commit, name, label);
	fFields.back().text = spec;
	return *this;
}


Form &Form::AddLabel(const std::string &text)
{
	return Add(FieldKind::Label, std::string(), text);
}


class FormDialog: public ModalDialog {
private:
	Form &fForm;
	git_repository *fRepository;
	const Snapshot *fSnapshot;
	// One control per field: BTextControl, BTextView, BCheckBox, BMenuField
	// or nullptr.
	std::vector<BView*> fControls;

	bool Accept() final;

public:
	FormDialog(Form &form, git_repository *repository, const Snapshot *snapshot);

	void MessageReceived(BMessage *message) final;
};


FormDialog::FormDialog(Form &form, git_repository *repository, const Snapshot *snapshot):
	ModalDialog(form.title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE),
	fForm(form),
	fRepository(repository),
	fSnapshot(snapshot)
{
	BGridLayout *grid = new BGridLayout(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING);
	grid->SetMinColumnWidth(1, 360);
	int32 row = 0;
	BView *focus = nullptr;
	for (size_t i = 0; i < form.Fields().size(); i++, row++) {
		const FormField &field = form.Fields()[i];
		BView *control = nullptr;
		switch (field.kind) {
			case FieldKind::Text:
			case FieldKind::Password:
			case FieldKind::Commit: {
				BTextControl *text = new BTextControl(field.name.c_str(), field.label.c_str(), field.text.c_str(), nullptr);
				if (field.kind == FieldKind::Password) {
					text->TextView()->HideTyping(true);
				}
				grid->AddItem(text->CreateLabelLayoutItem(), 0, row);
				grid->AddItem(text->CreateTextViewLayoutItem(), 1, row);
				if (field.kind == FieldKind::Commit && repository != nullptr && snapshot != nullptr) {
					BMessage *message = new BMessage(kMsgPickCommit);
					message->AddInt32("field", static_cast<int32>(i));
					grid->AddView(new BButton("pick", "Choose" B_UTF8_ELLIPSIS, message), 2, row);
				}
				control = text;
				break;
			}
			case FieldKind::MultiLine: {
				BTextView *textView = nullptr;
				BScrollView *scrollView = ReadOnlyText(textView, field.text, 360, 100);
				textView->MakeEditable(true);
				grid->AddView(new BStringView("label", field.label.c_str()), 0, row);
				grid->AddView(scrollView, 1, row, 2, 1);
				control = textView;
				break;
			}
			case FieldKind::Check: {
				BCheckBox *check = new BCheckBox(field.name.c_str(), field.label.c_str(), nullptr);
				check->SetValue(field.checked ? B_CONTROL_ON : B_CONTROL_OFF);
				grid->AddView(check, 1, row, 2, 1);
				control = check;
				break;
			}
			case FieldKind::Choice: {
				BPopUpMenu *menu = new BPopUpMenu("choice");
				for (const std::string &choice : field.choices) {
					menu->AddItem(new BMenuItem(choice.c_str(), nullptr));
				}
				if (BMenuItem *item = menu->ItemAt(field.choice)) {
					item->SetMarked(true);
				}
				BMenuField *menuField = new BMenuField(field.name.c_str(), field.label.c_str(), menu);
				grid->AddItem(menuField->CreateLabelLayoutItem(), 0, row);
				grid->AddItem(menuField->CreateMenuBarLayoutItem(), 1, row, 2, 1);
				control = menuField;
				break;
			}
			case FieldKind::Label:
				grid->AddView(new BStringView("label", field.label.c_str()), 0, row, 3, 1);
				break;
		}
		if (focus == nullptr && control != nullptr && field.kind != FieldKind::Check && field.kind != FieldKind::Choice) {
			focus = control;
		}
		fControls.push_back(control);
	}

	BButton *cancel = new BButton("Cancel", new BMessage(kMsgDialogCancel));
	BButton *ok = new BButton(form.okLabel.c_str(), new BMessage(kMsgDialogOk));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(grid)
		.Add(ButtonRow(cancel, ok));
	SetDefaultButton(ok);
	if (BTextControl *text = dynamic_cast<BTextControl*>(focus)) {
		text->MakeFocus(true);
	} else if (focus != nullptr) {
		focus->MakeFocus(true);
	}
}


bool FormDialog::Accept()
{
	for (size_t i = 0; i < fForm.Fields().size(); i++) {
		FormField &field = fForm.Fields()[i];
		BView *control = fControls[i];
		if (BTextControl *text = dynamic_cast<BTextControl*>(control)) {
			field.text = text->Text();
		} else if (BTextView *textView = dynamic_cast<BTextView*>(control)) {
			field.text = textView->Text();
		} else if (BCheckBox *check = dynamic_cast<BCheckBox*>(control)) {
			field.checked = check->Value() == B_CONTROL_ON;
		} else if (BMenuField *menuField = dynamic_cast<BMenuField*>(control)) {
			field.choice = menuField->Menu()->IndexOf(menuField->Menu()->FindMarked());
		}
	}
	return true;
}


void FormDialog::MessageReceived(BMessage *message)
{
	if (message->what != kMsgPickCommit) {
		ModalDialog::MessageReceived(message);
		return;
	}
	const int32 index = message->GetInt32("field", -1);
	if (index < 0 || static_cast<size_t>(index) >= fControls.size()) {
		return;
	}
	BTextControl *text = dynamic_cast<BTextControl*>(fControls[index]);
	if (text == nullptr) {
		return;
	}
	std::string spec = text->Text();
	if (PickCommit(fRepository, *fSnapshot, spec)) {
		text->SetText(spec.c_str());
	}
}


bool Form::Go(git_repository *repository, const Snapshot *snapshot)
{
	return (new FormDialog(*this, repository, snapshot))->Go();
}


//#pragma mark - CommitPicker

class IndexRow: public BRow {
public:
	int index;

	explicit IndexRow(int rowIndex):
		index(rowIndex)
	{
	}
};


std::string FormatTime(int64_t time)
{
	if (time == 0) {
		return std::string();
	}
	const time_t value = static_cast<time_t>(time);
	struct tm local;
	localtime_r(&value, &local);
	char text[32];
	strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local);
	return text;
}


class CommitPicker: public ModalDialog {
private:
	git_repository *fRepository;
	// Set for a history window that is not modal.
	RepositoryPtr fOwnRepository;
	std::string fRepositoryPath;
	std::string *fResult;
	BTextControl *fSpec {};
	BColumnListView *fList {};
	std::vector<CommitInfo> fCommits;

	void ShowHistoryOf(const std::string &spec);
	void ShowCommit();
	bool Accept() final;

public:
	// result is nullptr to only show the history; then the window may own
	// its repository and not be modal.
	CommitPicker(
		git_repository *repository,
		RepositoryPtr ownRepository,
		const Snapshot &snapshot,
		const std::string &spec,
		std::string *result
	);

	void MessageReceived(BMessage *message) final;
};


static void AddRefItem(BMenu *menu, const std::string &label, const std::string &spec, const std::string &current)
{
	BMessage *message = new BMessage(kMsgHistoryOf);
	message->AddString("spec", spec.c_str());
	BMenuItem *item = new BMenuItem(label.c_str(), message);
	item->SetMarked(spec == current);
	menu->AddItem(item);
}


CommitPicker::CommitPicker(
	git_repository *repository,
	RepositoryPtr ownRepository,
	const Snapshot &snapshot,
	const std::string &spec,
	std::string *result
):
	ModalDialog(
		result != nullptr ? "Choose a commit" : "History",
		ownRepository != nullptr ? B_NOT_ZOOMABLE : B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE,
		ownRepository != nullptr ? B_NORMAL_WINDOW_FEEL : B_MODAL_APP_WINDOW_FEEL
	),
	fRepository(repository),
	fOwnRepository(std::move(ownRepository)),
	fRepositoryPath(git_repository_commondir(repository)),
	fResult(result)
{
	BPopUpMenu *menu = new BPopUpMenu("refs");
	for (const WorktreeInfo &worktree : snapshot.worktrees) {
		if (worktree.commit.has_value()) {
			AddRefItem(menu, "Worktree " + worktree.path, OidString(*worktree.commit), spec);
		}
	}
	menu->AddSeparatorItem();
	for (const BranchInfo &branch : snapshot.branches) {
		AddRefItem(menu, "Branch " + branch.name, branch.name, spec);
	}
	for (const RemoteInfo &remote : snapshot.remotes) {
		menu->AddSeparatorItem();
		for (const RemoteCopyInfo &copy : remote.copies) {
			AddRefItem(menu, "Remote copy " + copy.name, copy.name, spec);
		}
	}
	if (!snapshot.tags.empty()) {
		menu->AddSeparatorItem();
	}
	for (const TagInfo &tag : snapshot.tags) {
		AddRefItem(menu, "Tag " + tag.name, "tags/" + tag.name, spec);
	}
	BMenuField *refField = new BMenuField("refs", "History of:", menu);

	fList = new BColumnListView("commits", B_NAVIGABLE, B_PLAIN_BORDER, true);
	fList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fList->AddColumn(new BStringColumn("Commit", 70, 40, 200, B_TRUNCATE_END), 0);
	fList->AddColumn(new BStringColumn("Summary", 360, 60, 2000, B_TRUNCATE_END), 1);
	fList->AddColumn(new BStringColumn("Author", 120, 40, 400, B_TRUNCATE_END), 2);
	fList->AddColumn(new BStringColumn("Date", 120, 40, 200, B_TRUNCATE_END), 3);
	fList->SetSortingEnabled(false);
	fList->SetSelectionMessage(new BMessage(kMsgCommitSelected));
	fList->SetInvocationMessage(new BMessage(kMsgCommitInvoked));
	fList->SetExplicitPreferredSize(BSize(700, 360));

	fSpec = new BTextControl("spec", "Commit:", spec.c_str(), nullptr);
	BButton *cancel = new BButton(result != nullptr ? "Cancel" : "Close", new BMessage(kMsgDialogCancel));
	BButton *ok = result != nullptr ? new BButton("Choose", new BMessage(kMsgDialogOk)) : nullptr;
	BButton *showChanges = new BButton("Show changes" B_UTF8_ELLIPSIS, new BMessage(kMsgShowCommit));
	BGroupLayout *buttons = ButtonRow(cancel, ok);
	buttons->AddView(0, showChanges);
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(refField)
		.Add(fList)
		.Add(fSpec)
		.Add(buttons);
	if (ok != nullptr) {
		SetDefaultButton(ok);
	} else {
		fSpec->Hide();
	}
	ShowHistoryOf(spec.empty() && !snapshot.worktrees.empty() && snapshot.worktrees[0].commit.has_value()
		? OidString(*snapshot.worktrees[0].commit) : spec);
}


void CommitPicker::ShowHistoryOf(const std::string &spec)
{
	fList->Clear();
	fCommits.clear();
	try {
		fCommits = History(fRepository, ResolveCommit(fRepository, spec), 1000);
	} catch (const GitError &e) {
		return;
	}
	for (size_t i = 0; i < fCommits.size(); i++) {
		const CommitInfo &commit = fCommits[i];
		IndexRow *row = new IndexRow(static_cast<int>(i));
		row->SetField(new BStringField(ShortId(commit.id).c_str()), 0);
		row->SetField(new BStringField(commit.summary.c_str()), 1);
		row->SetField(new BStringField(commit.author.c_str()), 2);
		row->SetField(new BStringField(FormatTime(commit.time).c_str()), 3);
		fList->AddRow(row);
	}
}


// Opens the changes of the selected or entered commit, above this window.
void CommitPicker::ShowCommit()
{
	try {
		const IndexRow *row = dynamic_cast<IndexRow*>(fList->CurrentSelection());
		const git_oid id = row != nullptr ? fCommits[row->index].id : ResolveCommit(fRepository, fSpec->Text());
		(new CommitWindow(fRepositoryPath, id, Feel()))->Show();
	} catch (const std::exception &e) {
		DiffKit::ShowError(e.what());
	}
}


bool CommitPicker::Accept()
{
	if (fResult == nullptr) {
		return true;
	}
	const std::string spec = fSpec->Text();
	try {
		ResolveCommit(fRepository, spec);
	} catch (const GitError &e) {
		fSpec->MakeFocus(true);
		return false;
	}
	*fResult = spec;
	return true;
}


void CommitPicker::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgHistoryOf: {
			const char *spec = message->GetString("spec", "");
			fSpec->SetText(spec);
			ShowHistoryOf(spec);
			break;
		}
		case kMsgCommitSelected: {
			const IndexRow *row = dynamic_cast<IndexRow*>(fList->CurrentSelection());
			if (row != nullptr) {
				fSpec->SetText(OidString(fCommits[row->index].id).c_str());
			}
			break;
		}
		case kMsgCommitInvoked:
			if (fResult == nullptr) {
				ShowCommit();
			} else if (Accept()) {
				Finish(true);
			}
			break;
		case kMsgShowCommit:
			ShowCommit();
			break;
		default:
			ModalDialog::MessageReceived(message);
			break;
	}
}


bool PickCommit(git_repository *repository, const Snapshot &snapshot, std::string &spec)
{
	std::string result;
	if (!(new CommitPicker(repository, RepositoryPtr(), snapshot, spec, &result))->Go()) {
		return false;
	}
	spec = result;
	return true;
}


void ShowHistory(const std::string &repositoryPath, const Snapshot &snapshot, const std::string &spec)
{
	RepositoryPtr repository = OpenRepository(repositoryPath);
	git_repository *handle = repository.get();
	CommitPicker *window = new CommitPicker(handle, std::move(repository), snapshot, spec, nullptr);
	window->ResizeToPreferred();
	window->CenterOnScreen();
	window->Show();
}


//#pragma mark - PlanDialog

class PlanDialog: public ModalDialog {
private:
	const Plan &fPlan;
	Resolution *fResolution;
	Resolution fChoice = Resolution::Cancel;
	BButton *fRun {};
	BCheckBox *fConfirm {};

	void UpdateRunButton();
	bool Accept() final;

public:
	PlanDialog(const Plan &plan, git_repository *repository, Resolution *resolution);

	void MessageReceived(BMessage *message) final;
};


static BRadioButton *ResolutionButton(const char *label, Resolution resolution)
{
	BMessage *message = new BMessage(kMsgResolution);
	message->AddInt32("resolution", static_cast<int32>(resolution));
	return new BRadioButton(label, label, message);
}


PlanDialog::PlanDialog(const Plan &plan, git_repository *repository, Resolution *resolution):
	ModalDialog(plan.title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE),
	fPlan(plan),
	fResolution(resolution)
{
	BTextView *textView = nullptr;
	BScrollView *text = ReadOnlyText(textView, plan.Describe(repository), 600, 280);
	BGroupLayout *layout = BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(text);

	const bool canRun = plan.blocked.empty();
	if (canRun && (plan.NeedsResolution() || plan.FilesLocked())) {
		layout->AddView(new BStringView("resolutionLabel", "The files of a worktree cannot simply be updated:"));
		BRadioButton *cancel = ResolutionButton("Cancel", Resolution::Cancel);
		cancel->SetValue(B_CONTROL_ON);
		layout->AddView(cancel);
		layout->AddView(ResolutionButton("Move position only, keep files and staged snapshot as they are",
			Resolution::PositionOnly));
		if (!plan.FilesLocked()) {
			layout->AddView(ResolutionButton("Discard the overlapping changes and update the files",
				Resolution::Discard));
		}
	}
	if (canRun) {
		fConfirm = new BCheckBox("confirm", "I understand that the listed data is lost", new BMessage(kMsgConfirmToggled));
		layout->AddView(fConfirm);
		fConfirm->Hide();
	}

	BButton *cancel = new BButton(canRun ? "Cancel" : "Close", new BMessage(kMsgDialogCancel));
	if (canRun) {
		fRun = new BButton(plan.network ? "Run in background" : "Run", new BMessage(kMsgDialogOk));
	}
	layout->AddItem(ButtonRow(cancel, fRun));
	if (fRun != nullptr) {
		SetDefaultButton(fRun);
	}
	UpdateRunButton();
}


void PlanDialog::UpdateRunButton()
{
	if (fRun == nullptr) {
		return;
	}
	const bool needsChoice = fPlan.NeedsResolution() || fPlan.FilesLocked();
	const bool destructive = fPlan.destructive || fChoice == Resolution::Discard;
	if (destructive && fConfirm->IsHidden()) {
		fConfirm->Show();
	} else if (!destructive && !fConfirm->IsHidden()) {
		fConfirm->Hide();
	}
	const bool confirmed = !destructive || fConfirm->Value() == B_CONTROL_ON;
	fRun->SetEnabled((!needsChoice || fChoice != Resolution::Cancel) && confirmed);
}


bool PlanDialog::Accept()
{
	if (!fRun->IsEnabled()) {
		return false;
	}
	*fResolution = fChoice;
	return true;
}


void PlanDialog::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgResolution:
			fChoice = static_cast<Resolution>(message->GetInt32("resolution", 0));
			UpdateRunButton();
			break;
		case kMsgConfirmToggled:
			UpdateRunButton();
			break;
		default:
			ModalDialog::MessageReceived(message);
			break;
	}
}


bool ConfirmPlan(const Plan &plan, git_repository *repository, Resolution &resolution)
{
	resolution = Resolution::Cancel;
	return (new PlanDialog(plan, repository, &resolution))->Go();
}


//#pragma mark - ListDialog

class ListDialog: public ModalDialog {
private:
	BColumnListView *fList {};
	std::vector<int> *fSelected;
	bool fChoosing;

	bool Accept() final;

public:
	ListDialog(
		const std::string &title,
		const std::string &okLabel,
		const std::vector<ListColumn> &columns,
		const std::vector<std::vector<std::string>> &rows,
		bool multiple,
		std::vector<int> *selected
	);

	void MessageReceived(BMessage *message) final;
};


ListDialog::ListDialog(
	const std::string &title,
	const std::string &okLabel,
	const std::vector<ListColumn> &columns,
	const std::vector<std::vector<std::string>> &rows,
	bool multiple,
	std::vector<int> *selected
):
	ModalDialog(title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE),
	fSelected(selected),
	fChoosing(!okLabel.empty())
{
	fList = new BColumnListView("list", B_NAVIGABLE, B_PLAIN_BORDER, true);
	fList->SetSelectionMode(multiple ? B_MULTIPLE_SELECTION_LIST : B_SINGLE_SELECTION_LIST);
	float width = 0;
	for (size_t i = 0; i < columns.size(); i++) {
		fList->AddColumn(new BStringColumn(columns[i].title.c_str(), columns[i].width, 30, 3000, B_TRUNCATE_END),
			static_cast<int32>(i));
		width += columns[i].width;
	}
	fList->SetSortingEnabled(false);
	fList->SetInvocationMessage(new BMessage(kMsgCommitInvoked));
	fList->SetExplicitPreferredSize(BSize(std::min(std::max(width + 20, 400.0f), 900.0f), 320));
	for (size_t i = 0; i < rows.size(); i++) {
		IndexRow *row = new IndexRow(static_cast<int>(i));
		for (size_t column = 0; column < rows[i].size() && column < columns.size(); column++) {
			row->SetField(new BStringField(rows[i][column].c_str()), static_cast<int32>(column));
		}
		fList->AddRow(row);
	}

	BButton *cancel = new BButton(fChoosing ? "Cancel" : "Close", new BMessage(kMsgDialogCancel));
	BButton *ok = fChoosing ? new BButton(okLabel.c_str(), new BMessage(kMsgDialogOk)) : nullptr;
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fList)
		.Add(ButtonRow(cancel, ok));
	if (ok != nullptr) {
		SetDefaultButton(ok);
	}
}


bool ListDialog::Accept()
{
	if (!fChoosing) {
		return true;
	}
	fSelected->clear();
	BRow *row = nullptr;
	while ((row = fList->CurrentSelection(row)) != nullptr) {
		fSelected->push_back(static_cast<IndexRow*>(row)->index);
	}
	return !fSelected->empty();
}


void ListDialog::MessageReceived(BMessage *message)
{
	if (message->what == kMsgCommitInvoked) {
		if (fChoosing && fList->SelectionMode() == B_SINGLE_SELECTION_LIST && Accept()) {
			Finish(true);
		}
		return;
	}
	ModalDialog::MessageReceived(message);
}


bool ChooseFromList(
	const std::string &title,
	const std::string &okLabel,
	const std::vector<ListColumn> &columns,
	const std::vector<std::vector<std::string>> &rows,
	bool multiple,
	std::vector<int> &selected
)
{
	return (new ListDialog(title, okLabel, columns, rows, multiple, &selected))->Go();
}


class TextDialog: public ModalDialog {
public:
	TextDialog(const std::string &title, const std::string &text):
		ModalDialog(title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE)
	{
		BTextView *textView = nullptr;
		BScrollView *scrollView = ReadOnlyText(textView, text, 600, 300);
		BButton *close = new BButton("Close", new BMessage(kMsgDialogCancel));
		BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
			.SetInsets(B_USE_WINDOW_SPACING)
			.Add(scrollView)
			.Add(ButtonRow(close, nullptr));
		SetDefaultButton(close);
	}
};


void ShowText(const std::string &title, const std::string &text)
{
	(new TextDialog(title, text))->Go();
}


} // GitTool
