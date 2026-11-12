#include "Dialogs.h"

#include <time.h>

#include <algorithm>
#include <functional>
#include <stdexcept>

#include <Button.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <GridLayout.h>
#include <GridView.h>
#include <GroupView.h>
#include <MessageFilter.h>
#include <Node.h>
#include <String.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <RadioButton.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>

#include <CodeEditView/CodeEditView.h>

#include "CommitWindow.h"
#include "DiffView.h"
#include "Ask.h"
#include "BackgroundLoader.h"
#include "MessageEditor.h"
#include "TruncatingLabel.h"
#include "Messages.h"
#include "Progress.h"
#include "WindowHelpers.h"


namespace GitTool {


//#pragma mark - Dialog

Dialog::Dialog(const char *title, uint32 flags):
	BWindow(
		BRect(0, 0, 100, 100),
		title,
		B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL,
		flags | B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE
	)
{
}


void Dialog::Open(BWindow *owner, std::function<void(bool accepted)> done)
{
	fAnswer = DiffKit::AnswerMessage(owner, [done = std::move(done)](BMessage *answer) {
		done(answer->GetBool("accepted", false));
	});
	fAnswerTarget = BMessenger(owner);
	fAnswerPending = true;
	DiffKit::AttachDialog(owner, this);
	ResizeToPreferred();
	DiffKit::PlaceOver(this, owner->Frame());
	Show();
}


void Dialog::SendAnswer(bool accepted)
{
	if (!fAnswerPending) {
		return;
	}
	fAnswerPending = false;
	BMessage answer(fAnswer);
	answer.AddBool("accepted", accepted);
	fAnswerTarget.SendMessage(&answer);
}


bool Dialog::Go()
{
	// A looper waiting here would freeze its windows; loopers use Open().
	if (BLooper::LooperForThread(find_thread(nullptr)) != nullptr) {
		debugger("Dialog::Go() called on a looper thread; use Open()");
	}
	bool accepted = false;
	fAccepted = &accepted;
	const sem_id semaphore = create_sem(0, "modal dialog");
	fSemaphore = semaphore;
	ResizeToPreferred();
	CenterOnScreen();
	Show();
	while (acquire_sem(semaphore) == B_INTERRUPTED) {
	}
	delete_sem(semaphore);
	return accepted;
}


void Dialog::Finish(bool accepted)
{
	SendAnswer(accepted);
	if (fAccepted != nullptr) {
		*fAccepted = accepted;
		fAccepted = nullptr;
		release_sem(fSemaphore);
	}
	Quit();
}


bool Dialog::QuitRequested()
{
	SendAnswer(false);
	if (fAccepted != nullptr) {
		*fAccepted = false;
		fAccepted = nullptr;
		release_sem(fSemaphore);
	}
	return true;
}


void Dialog::MessageReceived(BMessage *message)
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


// A label that does not limit the width of the column or group it is in.
static BStringView *Label(const char *name, const char *text)
{
	BStringView *label = new BStringView(name, text);
	label->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	return label;
}


// The label of a multi-line field, beside its first line.
static BStringView *TopLabel(const char *text)
{
	BStringView *label = new BStringView("label", text);
	label->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));
	return label;
}


// The column list view of the system ignores explicit sizes, so they are
// set on a group around it. The height is a minimum too: with height for
// width items in the layout (as a menu field's menu bar) the preferred
// height is lost.
static BGroupView *WithSize(BView *view, BSize size)
{
	BGroupView *group = new BGroupView(B_HORIZONTAL, 0);
	group->GroupLayout()->AddView(view);
	group->SetExplicitPreferredSize(size);
	group->SetExplicitMinSize(BSize(B_SIZE_UNSET, size.height));
	return group;
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


// Passes the first commit dropped from a history window to a function
// instead of the view it was dropped on.
class CommitDropFilter: public BMessageFilter {
private:
	std::function<void(const git_oid &commit)> fDropped;

public:
	explicit CommitDropFilter(std::function<void(const git_oid &commit)> dropped):
		BMessageFilter(B_DROPPED_DELIVERY, B_ANY_SOURCE, kMsgDragCommits),
		fDropped(std::move(dropped))
	{
	}

	filter_result Filter(BMessage *message, BHandler **target) final
	{
		(void)target;
		const void *data = nullptr;
		ssize_t size = 0;
		if (message->FindData("commit", B_RAW_TYPE, &data, &size) == B_OK && size == sizeof(git_oid)) {
			fDropped(*static_cast<const git_oid*>(data));
		}
		return B_SKIP_MESSAGE;
	}
};


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


Form &Form::AddMessage(const std::string &name, const std::string &label, const std::string &text)
{
	Add(FieldKind::Message, name, label);
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


Form &Form::AcceptPeople(const std::string &name)
{
	for (FormField &field : fFields) {
		if (field.name == name) {
			field.acceptsPeople = true;
		}
	}
	return *this;
}


// Adds the e-mail addresses of People files dropped on a text field;
// other drops go on to the field.
class PeopleDropFilter: public BMessageFilter {
private:
	BTextControl *fField;

public:
	explicit PeopleDropFilter(BTextControl *field):
		BMessageFilter(B_DROPPED_DELIVERY, B_ANY_SOURCE, B_SIMPLE_DATA),
		fField(field)
	{
	}

	filter_result Filter(BMessage *message, BHandler **target) final
	{
		(void)target;
		std::string added;
		entry_ref ref;
		for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
			BNode node(&ref);
			BString email;
			if (node.ReadAttrString("META:email", &email) == B_OK && email.Length() > 0) {
				added += (added.empty() ? "" : ", ") + std::string(email.String());
			}
		}
		if (added.empty()) {
			return B_DISPATCH_MESSAGE;
		}
		std::string text = fField->Text();
		while (!text.empty() && (text.back() == ' ' || text.back() == ',')) {
			text.pop_back();
		}
		fField->SetText((text.empty() ? added : text + ", " + added).c_str());
		return B_SKIP_MESSAGE;
	}
};


Form &Form::Derive(const std::string &target, const std::string &source,
	std::function<std::string(const std::string &sourceText)> derive)
{
	fDerivations.push_back({target, source, std::move(derive)});
	return *this;
}


static constexpr uint32 kMsgFieldChanged = 'GTfc';
static constexpr uint32 kMsgFormActionDone = 'GTfd';


class FormDialog: public Dialog {
private:
	// Shared with the owner's callback, which reads it after the answer.
	std::shared_ptr<Form> fForm;
	// Its own: the owner goes on using its handle and refreshing its
	// snapshot meanwhile.
	RepositoryPtr fRepository;
	std::optional<Snapshot> fSnapshot;
	// One control per field: BTextControl, BTextView, BCheckBox, BMenuField
	// or nullptr.
	std::vector<BView*> fControls;
	BButton *fOk {};
	DiffKit::TruncatingLabel *fStatus {};
	// Per derivation: the text it last gave its target; none once the user
	// edited the target.
	std::vector<std::optional<std::string>> fDerived;
	// With an action: the message that runs it on the owner's thread.
	std::optional<BMessage> fAction;
	BMessenger fActionTarget;

	size_t FieldIndex(const std::string &name) const;
	BTextControl *TextControl(const std::string &name) const;
	void UpdateDerived();
	void SetBusy(bool busy);
	bool Accept() final;

public:
	FormDialog(std::shared_ptr<Form> form, git_repository *repository, const Snapshot *snapshot);
	~FormDialog() override;

	// Before Open().
	void SetAction(const BMessage &action, const BMessenger &target);

	void MessageReceived(BMessage *message) final;
};


FormDialog::FormDialog(std::shared_ptr<Form> sharedForm, git_repository *repository, const Snapshot *snapshot):
	Dialog(sharedForm->title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE),
	fForm(std::move(sharedForm))
{
	Form &form = *fForm;
	if (repository != nullptr && snapshot != nullptr) {
		try {
			fRepository = OpenRepository(git_repository_commondir(repository));
			fSnapshot = *snapshot;
		} catch (const std::exception &e) {
			fRepository.reset();
		}
	}
	repository = fRepository.get();
	snapshot = fSnapshot.has_value() ? &*fSnapshot : nullptr;
	// The grid has a view, so the controls of its layout items are added to
	// the window.
	BGridView *gridView = new BGridView(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING);
	BGridLayout *grid = gridView->GridLayout();
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
				// The items fill the row's height by default, so a taller
				// button in the row would make the text field taller.
				BLayoutItem *labelItem = text->CreateLabelLayoutItem();
				BLayoutItem *textItem = text->CreateTextViewLayoutItem();
				labelItem->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));
				textItem->SetExplicitAlignment(BAlignment(B_ALIGN_USE_FULL_WIDTH, B_ALIGN_VERTICAL_CENTER));
				grid->AddItem(labelItem, 0, row);
				if (field.kind == FieldKind::Commit && repository != nullptr && snapshot != nullptr) {
					grid->AddItem(textItem, 1, row);
					BMessage *message = new BMessage(kMsgPickCommit);
					message->AddInt32("field", static_cast<int32>(i));
					grid->AddView(new BButton("pick", "Choose" B_UTF8_ELLIPSIS, message), 2, row);
				} else {
					grid->AddItem(textItem, 1, row, 2, 1);
				}
				if (field.acceptsPeople) {
					text->TextView()->AddFilter(new PeopleDropFilter(text));
				}
				control = text;
				break;
			}
			case FieldKind::MultiLine: {
				BTextView *textView = nullptr;
				BScrollView *scrollView = ReadOnlyText(textView, field.text, 360, 100);
				textView->MakeEditable(true);
				grid->AddView(TopLabel(field.label.c_str()), 0, row);
				grid->AddView(scrollView, 1, row, 2, 1);
				control = textView;
				break;
			}
			case FieldKind::Message: {
				CodeEditView *editor = GitDiff::CreateMessageEditor(field.name.c_str());
				GitDiff::SetMessageText(editor, field.text);
				editor->SetTarget(BMessenger(this));
				grid->AddView(TopLabel(field.label.c_str()), 0, row);
				grid->AddView(GitDiff::MessageScrollView("messageScroll", editor, 12), 1, row, 2, 1);
				control = editor;
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
				grid->AddView(Label("label", field.label.c_str()), 0, row, 3, 1);
				break;
		}
		if (focus == nullptr && control != nullptr && field.kind != FieldKind::Check && field.kind != FieldKind::Choice) {
			focus = control;
		}
		fControls.push_back(control);
	}

	BButton *cancel = new BButton("Cancel", new BMessage(kMsgDialogCancel));
	BButton *ok = new BButton(form.okLabel.c_str(), new BMessage(kMsgDialogOk));
	fOk = ok;
	fStatus = new DiffKit::TruncatingLabel("status", "");
	fStatus->SetExplicitMinSize(BSize(360, B_SIZE_UNSET));
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(gridView)
		.Add(fStatus)
		.Add(ButtonRow(cancel, ok));
	fStatus->Hide();
	SetDefaultButton(ok);
	DiffKit::KeepEnterInEditors(this);
	// A commit dropped from a history window fills the first commit field.
	for (size_t i = 0; i < fForm->Fields().size(); i++) {
		BTextControl *commitField = dynamic_cast<BTextControl*>(fControls[i]);
		if (fForm->Fields()[i].kind == FieldKind::Commit && commitField != nullptr) {
			AddCommonFilter(new CommitDropFilter([commitField](const git_oid &commit) {
				commitField->SetText(OidString(commit).c_str());
			}));
			break;
		}
	}
	if (BTextControl *text = dynamic_cast<BTextControl*>(focus)) {
		text->MakeFocus(true);
	} else if (focus != nullptr) {
		focus->MakeFocus(true);
	}

	// Derived fields start derived unless they have a text.
	for (const Form::Derivation &derivation : fForm->Derivations()) {
		BTextControl *source = TextControl(derivation.source);
		BTextControl *target = TextControl(derivation.target);
		std::optional<std::string> derived;
		if (source != nullptr && target != nullptr) {
			source->SetModificationMessage(new BMessage(kMsgFieldChanged));
			target->SetModificationMessage(new BMessage(kMsgFieldChanged));
			const std::string value = derivation.derive(source->Text());
			if (target->Text()[0] == '\0') {
				target->SetText(value.c_str());
			}
			if (value == target->Text()) {
				derived = value;
			}
		}
		fDerived.push_back(derived);
	}
}


FormDialog::~FormDialog()
{
	if (fAction.has_value()) {
		DiffKit::ForgetAnswer(*fAction);
	}
}


void FormDialog::SetAction(const BMessage &action, const BMessenger &target)
{
	fAction = action;
	fActionTarget = target;
}


size_t FormDialog::FieldIndex(const std::string &name) const
{
	for (size_t i = 0; i < fForm->Fields().size(); i++) {
		if (fForm->Fields()[i].name == name) {
			return i;
		}
	}
	return fControls.size();
}


BTextControl *FormDialog::TextControl(const std::string &name) const
{
	const size_t index = FieldIndex(name);
	return index < fControls.size() ? dynamic_cast<BTextControl*>(fControls[index]) : nullptr;
}


// After a text field changed: targets the user edited stop following their
// source; the others follow it.
void FormDialog::UpdateDerived()
{
	const std::vector<Form::Derivation> &derivations = fForm->Derivations();
	for (size_t i = 0; i < derivations.size() && i < fDerived.size(); i++) {
		BTextControl *source = TextControl(derivations[i].source);
		BTextControl *target = TextControl(derivations[i].target);
		if (source == nullptr || target == nullptr || !fDerived[i].has_value()) {
			continue;
		}
		if (*fDerived[i] != target->Text()) {
			fDerived[i].reset();
			continue;
		}
		const std::string value = derivations[i].derive(source->Text());
		if (value != target->Text()) {
			target->SetText(value.c_str());
		}
		fDerived[i] = value;
	}
}


// While the action runs, nothing can be changed.
void FormDialog::SetBusy(bool busy)
{
	for (BView *view : fControls) {
		if (BControl *control = dynamic_cast<BControl*>(view)) {
			control->SetEnabled(!busy);
		} else if (BMenuField *menuField = dynamic_cast<BMenuField*>(view)) {
			menuField->SetEnabled(!busy);
		} else if (BTextView *textView = dynamic_cast<BTextView*>(view)) {
			textView->MakeEditable(!busy);
		}
	}
	fOk->SetEnabled(!busy);
}


bool FormDialog::Accept()
{
	for (size_t i = 0; i < fForm->Fields().size(); i++) {
		FormField &field = fForm->Fields()[i];
		BView *control = fControls[i];
		if (BTextControl *text = dynamic_cast<BTextControl*>(control)) {
			field.text = text->Text();
		} else if (BTextView *textView = dynamic_cast<BTextView*>(control)) {
			field.text = textView->Text();
		} else if (CodeEditView *editor = dynamic_cast<CodeEditView*>(control)) {
			field.text = GitDiff::MessageText(editor);
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
	switch (message->what) {
		case kMsgFieldChanged:
			UpdateDerived();
			return;
		case kMsgDialogOk:
			if (!fAction.has_value()) {
				break;
			}
			if (fOk->IsEnabled() && Accept()) {
				SetBusy(true);
				fStatus->SetHighUIColor(B_PANEL_TEXT_COLOR);
				fStatus->SetLabel("Working" B_UTF8_ELLIPSIS);
				if (fStatus->IsHidden()) {
					fStatus->Show();
				}
				fActionTarget.SendMessage(&*fAction);
			}
			return;
		case kMsgFormActionDone: {
			const std::string error = message->GetString("error", "");
			if (error.empty()) {
				Finish(true);
				return;
			}
			SetBusy(false);
			fStatus->SetHighUIColor(B_FAILURE_COLOR);
			fStatus->SetLabel(error.c_str());
			return;
		}
		default:
			break;
	}
	if (message->what == CodeEdit::kMsgContentsChanged) {
		for (BView *control : fControls) {
			if (CodeEditView *editor = dynamic_cast<CodeEditView*>(control)) {
				GitDiff::UpdateMessageDecorations(editor);
			}
		}
		return;
	}
	if (message->what != kMsgPickCommit) {
		Dialog::MessageReceived(message);
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
	if (fRepository == nullptr || !fSnapshot.has_value()) {
		return;
	}
	PickCommit(this, fRepository.get(), *fSnapshot, text->Text(), [text](const std::string &spec) {
		text->SetText(spec.c_str());
	});
}


void Form::Open(BWindow *owner, std::function<void(const Form &form)> accepted, git_repository *repository,
	const Snapshot *snapshot, std::function<void()> cancelled) const
{
	std::shared_ptr<Form> form = std::make_shared<Form>(*this);
	FormDialog *dialog = new FormDialog(form, repository, snapshot);
	dialog->Open(owner, [form, accepted = std::move(accepted), cancelled = std::move(cancelled)](bool ok) {
		if (ok) {
			accepted(*form);
		} else if (cancelled) {
			cancelled();
		}
	});
}


void Form::OpenWithAction(BWindow *owner, Action action) const
{
	std::shared_ptr<Form> form = std::make_shared<Form>(*this);
	FormDialog *dialog = new FormDialog(form, nullptr, nullptr);
	const BMessenger dialogMessenger(dialog);
	const BMessage run = DiffKit::RepeatedAnswerMessage(owner,
		[form, action = std::move(action), dialogMessenger](BMessage *message) {
			(void)message;
			action(*form, [dialogMessenger](const std::string &error) {
				BMessage done(kMsgFormActionDone);
				done.AddString("error", error.c_str());
				dialogMessenger.SendMessage(&done);
			});
		});
	dialog->SetAction(run, BMessenger(owner));
	dialog->Open(owner, [](bool accepted) {
		(void)accepted;
	});
}


bool Form::Go(git_repository *repository, const Snapshot *snapshot)
{
	std::shared_ptr<Form> form = std::make_shared<Form>(*this);
	const bool accepted = (new FormDialog(form, repository, snapshot))->Go();
	*this = *form;
	return accepted;
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


static void AddRefItem(BMenu *menu, const std::string &label, const std::string &spec, const std::string &current)
{
	BMessage *message = new BMessage(kMsgHistoryOf);
	message->AddString("spec", spec.c_str());
	BMenuItem *item = new BMenuItem(label.c_str(), message);
	item->SetMarked(spec == current);
	menu->AddItem(item);
}


BPopUpMenu *RefMenu(const Snapshot &snapshot, const std::string &current)
{
	BPopUpMenu *menu = new BPopUpMenu("refs");
	FillRefMenu(menu, snapshot, current);
	return menu;
}


void FillRefMenu(BMenu *menu, const Snapshot &snapshot, const std::string &current)
{
	while (BMenuItem *item = menu->RemoveItem(static_cast<int32>(0))) {
		delete item;
	}
	for (const WorktreeInfo &worktree : snapshot.worktrees) {
		if (worktree.commit.has_value()) {
			AddRefItem(menu, "Worktree " + worktree.path, OidString(*worktree.commit), current);
		}
	}
	menu->AddSeparatorItem();
	for (const BranchInfo &branch : snapshot.branches) {
		AddRefItem(menu, "Branch " + branch.name, branch.name, current);
	}
	for (const RemoteInfo &remote : snapshot.remotes) {
		menu->AddSeparatorItem();
		for (const RemoteCopyInfo &copy : remote.copies) {
			AddRefItem(menu, "Remote copy " + copy.name, copy.name, current);
		}
	}
	// Repositories can have tens of thousands of tags; the menu lists the
	// last ones by name, others are entered as tags/<name>.
	constexpr size_t kMenuTags = 50;
	if (!snapshot.tags.empty()) {
		menu->AddSeparatorItem();
	}
	for (size_t i = 0; i < snapshot.tags.size() && i < kMenuTags; i++) {
		const TagInfo &tag = snapshot.tags[snapshot.tags.size() - 1 - i];
		AddRefItem(menu, "Tag " + tag.name, "tags/" + tag.name, current);
	}
	if (snapshot.tags.size() > kMenuTags) {
		BMenuItem *more = new BMenuItem(("(" + std::to_string(snapshot.tags.size() - kMenuTags)
			+ " more tags: enter tags/<name> below)").c_str(), nullptr);
		more->SetEnabled(false);
		menu->AddItem(more);
	}
}


static constexpr uint32 kMsgPickerHistoryLoaded = 'GTph';


class CommitPicker: public Dialog {
private:
	// Its own handle: the owner goes on using its own meanwhile.
	RepositoryPtr fRepository;
	std::string fRepositoryPath;
	// Shared with the owner's callback, which reads it after the answer.
	std::shared_ptr<std::string> fResult;
	BTextControl *fSpec {};
	BColumnListView *fList {};
	std::vector<CommitInfo> fCommits;
	std::unique_ptr<DiffKit::BackgroundLoader<std::vector<CommitInfo>>> fLoader;

	void ShowHistoryOf(const std::string &spec);
	void HistoryLoaded();
	void ShowCommit();
	bool Accept() final;

public:
	CommitPicker(git_repository *repository, const Snapshot &snapshot, const std::string &spec,
		std::shared_ptr<std::string> result);

	void MessageReceived(BMessage *message) final;
};


CommitPicker::CommitPicker(git_repository *repository, const Snapshot &snapshot, const std::string &spec,
	std::shared_ptr<std::string> result):
	Dialog("Choose a commit", B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE),
	fRepository(OpenRepository(git_repository_commondir(repository))),
	fRepositoryPath(git_repository_commondir(repository)),
	fResult(std::move(result)),
	fLoader(std::make_unique<DiffKit::BackgroundLoader<std::vector<CommitInfo>>>(BMessenger(this),
		kMsgPickerHistoryLoaded))
{
	BMenuField *refField = new BMenuField("refs", "History of:", RefMenu(snapshot, spec));

	fList = new BColumnListView("commits", B_NAVIGABLE, B_PLAIN_BORDER, true);
	fList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fList->AddColumn(new BStringColumn("Commit", 70, 40, 200, B_TRUNCATE_END), 0);
	fList->AddColumn(new BStringColumn("Summary", 360, 60, 2000, B_TRUNCATE_END), 1);
	fList->AddColumn(new BStringColumn("Author", 120, 40, 400, B_TRUNCATE_END), 2);
	fList->AddColumn(new BStringColumn("Date", 120, 40, 200, B_TRUNCATE_END), 3);
	fList->SetSortingEnabled(false);
	fList->SetSelectionMessage(new BMessage(kMsgCommitSelected));
	fList->SetInvocationMessage(new BMessage(kMsgCommitInvoked));

	fSpec = new BTextControl("spec", "Commit:", spec.c_str(), nullptr);
	BButton *showHistory = new BButton("Show history", new BMessage(kMsgHistoryOf));
	BButton *cancel = new BButton("Cancel", new BMessage(kMsgDialogCancel));
	BButton *ok = new BButton("Choose", new BMessage(kMsgDialogOk));
	BButton *showChanges = new BButton("Show changes" B_UTF8_ELLIPSIS, new BMessage(kMsgShowCommit));
	BGroupLayout *buttons = ButtonRow(cancel, ok);
	buttons->AddView(0, showChanges);
	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(refField)
		.Add(WithSize(fList, BSize(700, 360)))
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fSpec)
			.Add(showHistory)
		.End()
		.Add(buttons);
	SetDefaultButton(ok);
	// A commit dropped from a history window is chosen and its history shown.
	AddCommonFilter(new CommitDropFilter([this](const git_oid &commit) {
		const std::string id = OidString(commit);
		fSpec->SetText(id.c_str());
		ShowHistoryOf(id);
	}));
	ShowHistoryOf(spec.empty() && !snapshot.worktrees.empty() && snapshot.worktrees[0].commit.has_value()
		? OidString(*snapshot.worktrees[0].commit) : spec);
}


// The history is read on a thread.
void CommitPicker::ShowHistoryOf(const std::string &spec)
{
	fList->Clear();
	fCommits.clear();
	const std::string path = fRepositoryPath;
	fLoader->Load([path, spec]() {
		RepositoryPtr repository = OpenRepository(path);
		return History(repository.get(), ResolveCommit(repository.get(), spec), 1000);
	});
}


void CommitPicker::HistoryLoaded()
{
	std::string error;
	std::optional<std::vector<CommitInfo>> loaded = fLoader->Take(error);
	if (!loaded.has_value()) {
		return;
	}
	fList->Clear();
	fCommits = std::move(*loaded);
	// Added together: one by one, each addition takes time in proportion to
	// the rows before it.
	BList rows;
	for (size_t i = 0; i < fCommits.size(); i++) {
		const CommitInfo &commit = fCommits[i];
		IndexRow *row = new IndexRow(static_cast<int>(i));
		row->SetField(new BStringField(ShortId(commit.id).c_str()), 0);
		row->SetField(new BStringField(commit.summary.c_str()), 1);
		row->SetField(new BStringField(commit.author.c_str()), 2);
		row->SetField(new BStringField(FormatTime(commit.time).c_str()), 3);
		rows.AddItem(row);
	}
	fList->AddRows(&rows, -1, nullptr);
}


// Opens the changes of the selected or entered commit, above this window.
void CommitPicker::ShowCommit()
{
	try {
		const IndexRow *row = dynamic_cast<IndexRow*>(fList->CurrentSelection());
		const git_oid id = row != nullptr ? fCommits[row->index].id
			: ResolveCommit(fRepository.get(), fSpec->Text());
		CommitWindow *window = new CommitWindow(fRepositoryPath, id, B_NORMAL_WINDOW_FEEL);
		DiffKit::AttachDialog(this, window);
		DiffKit::PlaceOver(window, Frame());
		window->Show();
	} catch (const std::exception &e) {
		DiffKit::ShowError(e.what());
	}
}


bool CommitPicker::Accept()
{
	const std::string spec = fSpec->Text();
	try {
		ResolveCommit(fRepository.get(), spec);
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
			// From the menu, or of the entered commit.
			const char *spec = message->GetString("spec", nullptr);
			if (spec != nullptr) {
				fSpec->SetText(spec);
			}
			ShowHistoryOf(fSpec->Text());
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
			if (Accept()) {
				Finish(true);
			}
			break;
		case kMsgShowCommit:
			ShowCommit();
			break;
		case kMsgPickerHistoryLoaded:
			HistoryLoaded();
			break;
		default:
			Dialog::MessageReceived(message);
			break;
	}
}


void PickCommit(BWindow *owner, git_repository *repository, const Snapshot &snapshot, const std::string &spec,
	std::function<void(const std::string &spec)> chosen)
{
	std::shared_ptr<std::string> result = std::make_shared<std::string>();
	CommitPicker *picker = nullptr;
	try {
		picker = new CommitPicker(repository, snapshot, spec, result);
	} catch (const std::exception &e) {
		DiffKit::ShowError(e.what());
		return;
	}
	picker->Open(owner, [result, chosen = std::move(chosen)](bool accepted) {
		if (accepted) {
			chosen(*result);
		}
	});
}


//#pragma mark - PlanDialog

class PlanDialog: public Dialog {
private:
	const Plan fPlan;
	// Shared with the owner's callback, which reads it after the answer.
	std::shared_ptr<Resolution> fResolution;
	Resolution fChoice = Resolution::Cancel;
	BButton *fRun {};
	BCheckBox *fConfirm {};

	void UpdateRunButton();
	bool Accept() final;

public:
	PlanDialog(const Plan &plan, git_repository *repository, std::shared_ptr<Resolution> resolution);

	void MessageReceived(BMessage *message) final;
};


static BRadioButton *ResolutionButton(const char *label, Resolution resolution)
{
	BMessage *message = new BMessage(kMsgResolution);
	message->AddInt32("resolution", static_cast<int32>(resolution));
	return new BRadioButton(label, label, message);
}


PlanDialog::PlanDialog(const Plan &plan, git_repository *repository, std::shared_ptr<Resolution> resolution):
	Dialog(plan.title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE),
	fPlan(plan),
	fResolution(std::move(resolution))
{
	BTextView *textView = nullptr;
	BScrollView *text = ReadOnlyText(textView, plan.Describe(repository), 600, 280);
	BGroupLayout *layout = BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(text);

	const bool canRun = plan.blocked.empty();
	if (canRun && (plan.NeedsResolution() || plan.FilesLocked())) {
		layout->AddView(Label("resolutionLabel", "The files of a worktree cannot simply be updated:"));
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
		fRun = new BButton("Run", new BMessage(kMsgDialogOk));
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
			Dialog::MessageReceived(message);
			break;
	}
}


void ConfirmPlan(BWindow *owner, const Plan &plan, git_repository *repository,
	std::function<void(Resolution resolution)> run)
{
	if (plan.RunsWithoutPreview()) {
		run(Resolution::Cancel);
		return;
	}
	std::shared_ptr<Resolution> resolution = std::make_shared<Resolution>(Resolution::Cancel);
	(new PlanDialog(plan, repository, resolution))->Open(owner, [resolution, run = std::move(run)](bool accepted) {
		if (accepted) {
			run(*resolution);
		}
	});
}


void RunPlanWithProgress(
	BWindow *owner,
	const std::string &repositoryPath,
	const Plan &plan,
	Resolution resolution,
	std::function<void(const std::string &error)> done
)
{
	DiffKit::RunWithProgress(owner, plan.title, [repositoryPath, plan, resolution](DiffKit::Progress &progress) {
		(void)progress;
		RepositoryPtr repository = OpenRepository(repositoryPath);
		RunContext context;
		context.repository = repository.get();
		context.resolution = resolution;
		plan.run(context);
	}, std::move(done));
}


//#pragma mark - ListDialog

class ListDialog: public Dialog {
private:
	BColumnListView *fList {};
	// Shared with the owner's callback, which reads it after the answer.
	std::shared_ptr<std::vector<int>> fSelected;
	bool fChoosing;

	bool Accept() final;

public:
	ListDialog(
		const std::string &title,
		const std::string &okLabel,
		const std::vector<ListColumn> &columns,
		const std::vector<std::vector<std::string>> &rows,
		bool multiple,
		std::shared_ptr<std::vector<int>> selected
	);

	void MessageReceived(BMessage *message) final;
};


ListDialog::ListDialog(
	const std::string &title,
	const std::string &okLabel,
	const std::vector<ListColumn> &columns,
	const std::vector<std::vector<std::string>> &rows,
	bool multiple,
	std::shared_ptr<std::vector<int>> selected
):
	Dialog(title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE),
	fSelected(std::move(selected)),
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
		.Add(WithSize(fList, BSize(std::min(std::max(width + 20, 400.0f), 900.0f), 320)))
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
	Dialog::MessageReceived(message);
}


void ChooseFromList(
	BWindow *owner,
	const std::string &title,
	const std::string &okLabel,
	const std::vector<ListColumn> &columns,
	const std::vector<std::vector<std::string>> &rows,
	bool multiple,
	std::function<void(const std::vector<int> &selected)> chosen
)
{
	std::shared_ptr<std::vector<int>> selected = std::make_shared<std::vector<int>>();
	(new ListDialog(title, okLabel, columns, rows, multiple, selected))->Open(owner,
		[selected, chosen = std::move(chosen)](bool accepted) {
			if (accepted && chosen) {
				chosen(*selected);
			}
		});
}


class TextDialog: public Dialog {
public:
	TextDialog(const std::string &title, const std::string &text):
		Dialog(title.c_str(), B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE)
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


void ShowText(BWindow *owner, const std::string &title, const std::string &text)
{
	(new TextDialog(title, text))->Open(owner, [](bool accepted) {
		(void)accepted;
	});
}


} // GitTool
