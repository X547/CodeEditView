#include "HistoryWindow.h"

#include <algorithm>
#include <exception>
#include <set>

#include <Alert.h>
#include <Beep.h>
#include <Button.h>
#include <CheckBox.h>
#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>

#include "CommitWindow.h"
#include "DiffView.h"
#include "Dialogs.h"
#include "Messages.h"


namespace GitTool {


using DiffKit::ShowError;


static constexpr size_t kHistoryLimit = 1000;


enum {
	kStateColumn,
	kCommitColumn,
	kSummaryColumn,
	kAuthorColumn,
	kDateColumn,
};


class CommitRow: public BRow {
public:
	git_oid commit;
	// Index in the edited steps; -1 for a row that is not a step.
	int32 step;

	CommitRow(const git_oid &rowCommit, int32 rowStep):
		commit(rowCommit),
		step(rowStep)
	{
	}
};


// The commit list; selected rows are dragged as kMsgDragCommits, and drops
// are posted to the window with the row they fell on.
class CommitList: public BColumnListView {
public:
	CommitList():
		BColumnListView("commits", B_NAVIGABLE, B_PLAIN_BORDER, true)
	{
	}

	bool InitiateDrag(BPoint point, bool wasSelected) final
	{
		(void)point;
		(void)wasSelected;
		BMessage message(kMsgDragCommits);
		message.AddMessenger("source", BMessenger(Window()));
		int32 count = 0;
		BRow *row = nullptr;
		while ((row = CurrentSelection(row)) != nullptr) {
			const CommitRow *commitRow = dynamic_cast<CommitRow*>(row);
			if (commitRow == nullptr) {
				continue;
			}
			message.AddData("commit", B_RAW_TYPE, &commitRow->commit, sizeof(git_oid));
			if (commitRow->step >= 0) {
				message.AddInt32("step", commitRow->step);
			}
			count++;
		}
		if (count == 0) {
			return false;
		}
		BPoint where;
		uint32 buttons;
		get_mouse(&where, &buttons);
		ConvertFromScreen(&where);
		DragMessage(&message, BRect(where.x - 40, where.y - 8, where.x + 200, where.y + 8));
		return true;
	}

	void MessageDropped(BMessage *message, BPoint point) final
	{
		if (message->what != kMsgDragCommits) {
			return;
		}
		BMessage dropped(*message);
		BRow *row = RowAt(point);
		dropped.AddInt32("row", row != nullptr ? IndexOf(row) : -1);
		Window()->PostMessage(&dropped);
	}
};


static std::string FirstLine(const std::string &text)
{
	return text.substr(0, text.find('\n'));
}


static std::string StateText(const EditStep &step, const StepResult &result)
{
	std::string text;
	switch (result.state) {
		case StepState::Kept:
			break;
		case StepState::Rewritten:
			text = step.squash ? "combined" : step.message.has_value() ? "reworded" : "rewritten";
			break;
		case StepState::Empty:
			text = "empty, left out";
			break;
		case StepState::Conflict:
			text = "⚠ conflict";
			break;
		case StepState::Pending:
			text = "pending";
			break;
	}
	if (step.copied) {
		text = text.empty() ? "copied" : "copied, " + text;
	}
	return text;
}


HistoryWindow::HistoryWindow(const std::string &repositoryPath, const std::string &spec, bool edit):
	BWindow(
		BRect(120, 120, 979, 719),
		"History",
		B_DOCUMENT_WINDOW,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
	),
	fRepositoryPath(repositoryPath),
	fRepository(OpenRepository(repositoryPath))
{
	Reload();
	fRefField = new BMenuField("refs", "History of:", RefMenu(fSnapshot, spec));
	fSpecField = new BTextControl("spec", nullptr, spec.c_str(), nullptr);
	BButton *show = new BButton("Show", new BMessage(kMsgHistoryOf));
	fEditMode = new BCheckBox("edit", "Edit", new BMessage(kMsgEditMode));

	fList = new CommitList();
	fList->SetSelectionMode(B_MULTIPLE_SELECTION_LIST);
	fList->AddColumn(new BStringColumn("Edit", 110, 40, 300, B_TRUNCATE_END), kStateColumn);
	fList->AddColumn(new BStringColumn("Commit", 70, 40, 200, B_TRUNCATE_END), kCommitColumn);
	fList->AddColumn(new BStringColumn("Summary", 380, 60, 2000, B_TRUNCATE_END), kSummaryColumn);
	fList->AddColumn(new BStringColumn("Author", 120, 40, 400, B_TRUNCATE_END), kAuthorColumn);
	fList->AddColumn(new BStringColumn("Date", 120, 40, 200, B_TRUNCATE_END), kDateColumn);
	fList->SetSortingEnabled(false);
	fList->SetSelectionMessage(new BMessage(kMsgCommitSelected));
	fList->SetInvocationMessage(new BMessage(kMsgCommitInvoked));

	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fShowChanges = new BButton("Show changes" B_UTF8_ELLIPSIS, new BMessage(kMsgShowCommit));
	fRemove = new BButton("Remove", new BMessage(kMsgRemoveSteps));
	fCombine = new BButton("Combine with older", new BMessage(kMsgCombineStep));
	fReword = new BButton("Reword" B_UTF8_ELLIPSIS, new BMessage(kMsgRewordStep));
	fReset = new BButton("Reset", new BMessage(kMsgResetEdits));
	fApply = new BButton("Apply" B_UTF8_ELLIPSIS, new BMessage(kMsgApplyEdits));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_SMALL_SPACING)
		.SetInsets(B_USE_SMALL_SPACING)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fRefField)
			.Add(fSpecField, 1)
			.Add(show)
			.Add(fEditMode)
		.End()
		.Add(fList)
		.Add(fStatus)
		.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
			.Add(fShowChanges)
			.Add(fRemove)
			.Add(fCombine)
			.Add(fReword)
			.AddGlue()
			.Add(fReset)
			.Add(fApply)
		.End();

	ShowHistoryOf(spec);
	if (edit && fEditMode->IsEnabled()) {
		fEditMode->SetValue(B_CONTROL_ON);
		SetEditMode(true);
	}
}


void HistoryWindow::Reload()
{
	try {
		fSnapshot = Snapshot::Load(fRepository.get());
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
}


void HistoryWindow::ShowHistoryOf(const std::string &spec)
{
	if (!ConfirmDiscard()) {
		return;
	}
	fEdit.reset();
	fEditMode->SetValue(B_CONTROL_OFF);
	fSpec = spec;
	fSpecField->SetText(spec.c_str());
	SetTitle(("History of " + spec).c_str());
	fCommits.clear();
	try {
		fCommits = History(fRepository.get(), ResolveCommit(fRepository.get(), spec), kHistoryLimit);
	} catch (const std::exception &e) {
		fStatus->SetText(e.what());
	}
	FillList();
}


void HistoryWindow::FillList()
{
	std::set<std::string> selected;
	for (const git_oid &commit : SelectedCommits()) {
		selected.insert(OidString(commit));
	}
	fList->Clear();
	fList->SetColumnVisible(kStateColumn, fEdit != nullptr);

	auto addRow = [this](const git_oid &commit, int32 step, const std::string &state, const std::string &summary) {
		CommitRow *row = new CommitRow(commit, step);
		CommitInfo info;
		try {
			info = ReadCommit(fRepository.get(), commit);
		} catch (const GitError &e) {
		}
		row->SetField(new BStringField(state.c_str()), kStateColumn);
		row->SetField(new BStringField(ShortId(commit).c_str()), kCommitColumn);
		row->SetField(new BStringField(summary.empty() ? info.summary.c_str() : summary.c_str()), kSummaryColumn);
		row->SetField(new BStringField(info.author.c_str()), kAuthorColumn);
		row->SetField(new BStringField(FormatTime(info.time).c_str()), kDateColumn);
		fList->AddRow(row);
		return row;
	};

	std::vector<CommitRow*> rows;
	if (fEdit != nullptr) {
		const std::vector<EditStep> &steps = fEdit->Steps();
		const std::vector<StepResult> &results = fEdit->Results();
		for (size_t i = steps.size(); i-- > 0;) {
			rows.push_back(addRow(steps[i].commit, static_cast<int32>(i), StateText(steps[i], results[i]),
				FirstLine(fEdit->Message(i))));
		}
		if (fEdit->Base().has_value()) {
			rows.push_back(addRow(*fEdit->Base(), -1, "not editable", std::string()));
		}
	} else {
		for (const CommitInfo &commit : fCommits) {
			rows.push_back(addRow(commit.id, -1, std::string(), std::string()));
		}
	}
	for (CommitRow *row : rows) {
		if (selected.count(OidString(row->commit)) > 0) {
			fList->AddToSelection(row);
		}
	}
	UpdateControls();
}


std::vector<size_t> HistoryWindow::SelectedSteps() const
{
	std::vector<size_t> steps;
	BRow *row = nullptr;
	while ((row = fList->CurrentSelection(row)) != nullptr) {
		const CommitRow *commitRow = dynamic_cast<CommitRow*>(row);
		if (commitRow != nullptr && commitRow->step >= 0) {
			steps.push_back(static_cast<size_t>(commitRow->step));
		}
	}
	return steps;
}


std::vector<git_oid> HistoryWindow::SelectedCommits() const
{
	std::vector<git_oid> commits;
	BRow *row = nullptr;
	while ((row = fList->CurrentSelection(row)) != nullptr) {
		if (const CommitRow *commitRow = dynamic_cast<CommitRow*>(row)) {
			commits.push_back(commitRow->commit);
		}
	}
	return commits;
}


void HistoryWindow::UpdateControls()
{
	const bool isBranch = fSnapshot.FindBranch(fSpec) != nullptr;
	fEditMode->SetEnabled(isBranch);
	const bool editing = fEdit != nullptr;
	const std::vector<size_t> steps = SelectedSteps();
	fShowChanges->SetEnabled(SelectedCommits().size() == 1);
	fRemove->SetEnabled(editing && !steps.empty());
	fCombine->SetEnabled(editing && steps.size() == 1 && steps[0] > 0);
	fReword->SetEnabled(editing && steps.size() == 1);
	fReset->SetEnabled(editing && fEdit->HasChanges());
	fApply->SetEnabled(editing && fEdit->HasChanges() && fEdit->Problem().empty());
	if (editing && steps.size() == 1) {
		fCombine->SetLabel(fEdit->Steps()[steps[0]].squash ? "Separate from older" : "Combine with older");
	}

	std::string status;
	if (!editing) {
		status = std::to_string(fCommits.size()) + " commits"
			+ (fCommits.size() >= kHistoryLimit ? " (the newest " + std::to_string(kHistoryLimit) + ")" : "");
		if (isBranch) {
			status += ". Enable Edit to reorder (drag), remove, combine and reword commits.";
		}
	} else if (!fEdit->HasChanges()) {
		status = "No changes. Drag commits to reorder them, or drop commits from another history window to copy them.";
	} else {
		status = fEdit->Problem();
		if (status.empty()) {
			const size_t first = *fEdit->FirstChanged();
			const std::optional<git_oid> base = first == 0 ? fEdit->Base()
				: std::optional<git_oid>(fEdit->Original()[first - 1]);
			status = "Pending changes; the commits after "
				+ (base.has_value() ? ShortId(*base) : std::string("the start")) + " are rewritten on Apply.";
		}
		if (steps.size() == 1 && !fEdit->Results()[steps[0]].conflicts.empty()) {
			status += " Conflicts:";
			for (const std::string &path : fEdit->Results()[steps[0]].conflicts) {
				status += " " + path;
			}
		}
	}
	fStatus->SetText(status.c_str());
}


bool HistoryWindow::ConfirmDiscard()
{
	if (fEdit == nullptr || !fEdit->HasChanges()) {
		return true;
	}
	BAlert *alert = new BAlert("History", "Discard the pending history edits?", "Cancel", "Discard", nullptr,
		B_WIDTH_AS_USUAL, B_WARNING_ALERT);
	alert->SetShortcut(0, B_ESCAPE);
	return alert->Go() == 1;
}


void HistoryWindow::SetEditMode(bool edit)
{
	if (!edit) {
		if (!ConfirmDiscard()) {
			fEditMode->SetValue(B_CONTROL_ON);
			return;
		}
		fEdit.reset();
		FillList();
		return;
	}
	try {
		fEdit = std::make_unique<HistoryEdit>(fRepository.get(), fSpec, kHistoryLimit);
	} catch (const std::exception &e) {
		fEditMode->SetValue(B_CONTROL_OFF);
		ShowError(e.what());
		return;
	}
	FillList();
}


// Within this window the steps move; from another window the commits are
// copied in. A drop on a row puts the commits above it (newer).
void HistoryWindow::Dropped(BMessage *message)
{
	if (fEdit == nullptr) {
		beep();
		return;
	}
	const size_t count = fEdit->Steps().size();
	const int32 row = message->GetInt32("row", -1);
	const size_t position = row < 0 || static_cast<size_t>(row) >= count ? 0 : count - row;
	BMessenger source;
	message->FindMessenger("source", &source);
	try {
		if (source == BMessenger(this) && message->HasInt32("step")) {
			std::vector<size_t> steps;
			int32 step;
			for (int32 i = 0; message->FindInt32("step", i, &step) == B_OK; i++) {
				steps.push_back(static_cast<size_t>(step));
			}
			fEdit->Move(steps, position);
		} else {
			std::vector<git_oid> commits;
			const void *data = nullptr;
			ssize_t size = 0;
			for (int32 i = 0; message->FindData("commit", B_RAW_TYPE, i, &data, &size) == B_OK; i++) {
				if (size == sizeof(git_oid)) {
					commits.push_back(*static_cast<const git_oid*>(data));
				}
			}
			// The drag lists the newest first; steps go oldest first.
			std::reverse(commits.begin(), commits.end());
			fEdit->Insert(commits, position);
		}
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
	FillList();
}


void HistoryWindow::Remove()
{
	fEdit->Remove(SelectedSteps());
	FillList();
}


void HistoryWindow::Combine()
{
	const std::vector<size_t> steps = SelectedSteps();
	if (steps.size() != 1) {
		return;
	}
	fEdit->SetSquash(steps[0], !fEdit->Steps()[steps[0]].squash);
	FillList();
}


void HistoryWindow::Reword()
{
	const std::vector<size_t> steps = SelectedSteps();
	if (steps.size() != 1) {
		return;
	}
	Form form("Reword " + ShortId(fEdit->Steps()[steps[0]].commit), "OK");
	form.AddMessage("message", "Message:", fEdit->Message(steps[0]));
	if (form.Go()) {
		fEdit->SetMessage(steps[0], form.Text("message"));
		FillList();
	}
}


void HistoryWindow::Apply()
{
	Reload();
	const Plan plan = fEdit->PlanApply(fSnapshot);
	Resolution resolution = Resolution::Cancel;
	if (!ConfirmPlan(plan, fRepository.get(), resolution)) {
		return;
	}
	RunContext context;
	context.repository = fRepository.get();
	context.resolution = resolution;
	try {
		plan.run(context);
	} catch (const std::exception &e) {
		ShowError(e.what());
		return;
	}
	Reload();
	fEdit.reset();
	ShowHistoryOf(fSpec);
	fEditMode->SetValue(B_CONTROL_ON);
	SetEditMode(true);
}


// The commit as it is shown: the rewritten one for a replayed step.
void HistoryWindow::ShowChanges()
{
	const CommitRow *row = dynamic_cast<CommitRow*>(fList->CurrentSelection());
	if (row == nullptr) {
		return;
	}
	git_oid commit = row->commit;
	if (fEdit != nullptr && row->step >= 0) {
		const StepResult &result = fEdit->Results()[row->step];
		if (result.state == StepState::Rewritten) {
			commit = result.commit;
		}
	}
	try {
		(new CommitWindow(fRepositoryPath, commit, B_NORMAL_WINDOW_FEEL))->Show();
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
}


void HistoryWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgHistoryOf: {
			const char *spec = message->GetString("spec", nullptr);
			ShowHistoryOf(spec != nullptr ? spec : fSpecField->Text());
			break;
		}
		case kMsgEditMode:
			SetEditMode(fEditMode->Value() == B_CONTROL_ON);
			break;
		case kMsgCommitSelected:
			UpdateControls();
			break;
		case kMsgCommitInvoked:
		case kMsgShowCommit:
			ShowChanges();
			break;
		case kMsgRemoveSteps:
			Remove();
			break;
		case kMsgCombineStep:
			Combine();
			break;
		case kMsgRewordStep:
			Reword();
			break;
		case kMsgResetEdits:
			if (ConfirmDiscard()) {
				fEdit->Reset();
				FillList();
			}
			break;
		case kMsgApplyEdits:
			Apply();
			break;
		case kMsgDragCommits:
			Dropped(message);
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


bool HistoryWindow::QuitRequested()
{
	return ConfirmDiscard();
}


} // GitTool
