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
#include <MessageFilter.h>
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
	kAddedColumn,
	kDeletedColumn,
	kFilesColumn,
};


class CommitRow: public BRow {
public:
	git_oid commit;
	// Index in the edited steps; -1 for a row that is not a step.
	int32 step;
	// The commit whose statistics the row shows: the rewritten one for a
	// replayed step.
	git_oid statsCommit;

	CommitRow(const git_oid &rowCommit, int32 rowStep, const git_oid &rowStatsCommit):
		commit(rowCommit),
		step(rowStep),
		statsCommit(rowStatsCommit)
	{
	}
};


class StatsField: public BField {
public:
	git_oid commit;

	explicit StatsField(const git_oid &fieldCommit):
		commit(fieldCommit)
	{
	}
};


// Added or deleted lines or changed files of the row's commit. They are
// computed in the background when first drawn; "…" until then.
class StatsColumn: public BTitledColumn {
public:
	enum class Kind {
		Added,
		Deleted,
		Files,
	};

private:
	Kind fKind;
	StatsLoader &fLoader;

public:
	StatsColumn(const char *title, float width, Kind kind, StatsLoader &loader):
		BTitledColumn(title, width, 30, 200, B_ALIGN_RIGHT),
		fKind(kind),
		fLoader(loader)
	{
	}

	void DrawField(BField *field, BRect rect, BView *parent) final
	{
		const git_oid &commit = static_cast<StatsField*>(field)->commit;
		const std::optional<CommitStats> stats = StatsLoader::Cached(commit);
		const rgb_color textColor = parent->HighColor();
		std::string text;
		rgb_color color = textColor;
		if (!stats.has_value()) {
			fLoader.Request(commit);
			text = B_UTF8_ELLIPSIS;
			color = mix_color(textColor, parent->LowColor(), 128);
		} else {
			switch (fKind) {
				case Kind::Added:
					text = stats->added > 0 ? "+" + std::to_string(stats->added) : std::string();
					color = ui_color(B_SUCCESS_COLOR);
					break;
				case Kind::Deleted:
					text = stats->deleted > 0 ? "−" + std::to_string(stats->deleted) : std::string();
					color = ui_color(B_FAILURE_COLOR);
					break;
				case Kind::Files:
					text = std::to_string(stats->files);
					break;
			}
		}
		parent->SetHighColor(color);
		DrawString(text.c_str(), parent, rect);
		parent->SetHighColor(textColor);
	}

	bool AcceptsField(const BField *field) const final
	{
		return dynamic_cast<const StatsField*>(field) != nullptr;
	}
};


// The commit list. Selected rows are dragged as kMsgDragCommits; while
// commits are dragged over a list that takes drops, a line marks where they
// would go, and the drop is posted to the window with that place. Delete
// and Backspace post kMsgRemoveSteps.
//
// The rows are drawn by a private child view of BColumnListView, so a
// message filter on it follows the mouse and keys, and the marker is a
// child of it that scrolls with the rows.
class CommitList: public BColumnListView {
private:
	class RowsFilter: public BMessageFilter {
	private:
		CommitList &fList;

	public:
		explicit RowsFilter(CommitList &list):
			BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
			fList(list)
		{
		}

		filter_result Filter(BMessage *message, BHandler **target) final
		{
			(void)target;
			return fList.FilterMessage(message);
		}
	};

	BView *fMarker {};
	// Rows above which commits can be inserted: 0 to fDropLimit (after the
	// last of them); -1 if the list takes no drops.
	int32 fDropLimit = -1;

	// The place for a drop at point (in the rows view): the index of the row
	// the commits go above, by which half of a row the point is in.
	int32 DropIndex(BPoint point)
	{
		int32 index = CountRows();
		if (BRow *row = RowAt(point)) {
			BRect rect;
			GetRowRect(row, &rect);
			index = IndexOf(row) + (point.y > (rect.top + rect.bottom) / 2 ? 1 : 0);
		}
		return std::min(index, fDropLimit);
	}

	void ShowMarker(int32 index)
	{
		float y = 0;
		if (index < CountRows()) {
			BRect rect;
			GetRowRect(RowAt(index), &rect);
			y = rect.top - 1;
		} else if (CountRows() > 0) {
			BRect rect;
			GetRowRect(RowAt(CountRows() - 1), &rect);
			y = rect.bottom + 1;
		}
		BView *rows = ScrollView();
		const float height = 2;
		fMarker->MoveTo(rows->Bounds().left, y - height / 2);
		fMarker->ResizeTo(rows->Bounds().Width(), height - 1);
		if (fMarker->IsHidden()) {
			fMarker->Show();
		}
	}

	void HideMarker()
	{
		if (!fMarker->IsHidden()) {
			fMarker->Hide();
		}
	}

	filter_result FilterMessage(BMessage *message)
	{
		switch (message->what) {
			case B_KEY_DOWN: {
				const char *bytes = message->GetString("bytes", "");
				if (bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE) {
					Window()->PostMessage(kMsgRemoveSteps);
					return B_SKIP_MESSAGE;
				}
				break;
			}
			case B_MOUSE_MOVED: {
				BMessage drag;
				const bool dragging = message->FindMessage("be:drag_message", &drag) == B_OK
					&& drag.what == kMsgDragCommits;
				const int32 transit = message->GetInt32("be:transit", B_OUTSIDE_VIEW);
				BPoint where;
				if (!dragging || fDropLimit < 0 || transit == B_EXITED_VIEW || transit == B_OUTSIDE_VIEW
					|| message->FindPoint("be:view_where", &where) != B_OK) {
					HideMarker();
					break;
				}
				ShowMarker(DropIndex(where));
				break;
			}
			case B_MOUSE_UP:
				HideMarker();
				break;
			default:
				break;
		}
		return B_DISPATCH_MESSAGE;
	}

public:
	CommitList():
		BColumnListView("commits", B_NAVIGABLE, B_PLAIN_BORDER, true)
	{
		fMarker = new BView(BRect(0, 0, 10, 1), "dropMarker", B_FOLLOW_NONE, B_WILL_DRAW);
		fMarker->SetViewUIColor(B_CONTROL_HIGHLIGHT_COLOR);
		fMarker->Hide();
		ScrollView()->AddChild(fMarker);
		ScrollView()->AddFilter(new RowsFilter(*this));
	}

	// The number of rows commits can be dropped among; -1 for no drops.
	void SetDropLimit(int32 limit)
	{
		fDropLimit = limit;
	}

	bool InitiateDrag(BPoint point, bool wasSelected) final
	{
		(void)point;
		(void)wasSelected;
		BMessage message(kMsgDragCommits);
		message.AddMessenger("source", BMessenger(Window()));
		// The drag rectangle covers the selected rows that are visible.
		BRect dragRect;
		BView *rows = ScrollView();
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
			BRect rect;
			if (GetRowRect(row, &rect)) {
				rect = rect & rows->Bounds();
				if (rect.IsValid()) {
					dragRect = dragRect.IsValid() ? dragRect | rect : rect;
				}
			}
		}
		if (!message.HasData("commit", B_RAW_TYPE)) {
			return false;
		}
		if (!dragRect.IsValid()) {
			BPoint where;
			uint32 buttons;
			rows->GetMouse(&where, &buttons, false);
			dragRect.Set(where.x, where.y, where.x + 200, where.y + 16);
		}
		rows->DragMessage(&message, dragRect);
		return true;
	}

	void MessageDropped(BMessage *message, BPoint point) final
	{
		HideMarker();
		if (message->what != kMsgDragCommits || fDropLimit < 0) {
			return;
		}
		BMessage dropped(*message);
		dropped.AddInt32("index", DropIndex(point));
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
		case StepState::Rewritten: {
			bool resolved = false;
			for (const auto &[path, file] : step.files) {
				resolved = resolved || file.resolves.has_value();
			}
			text = step.squash ? "combined" : step.completesPrevious ? "split, rest" : resolved ? "resolved"
				: !step.files.empty() ? "edited" : step.message.has_value() ? "reworded" : "rewritten";
			break;
		}
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
	fRepository(OpenRepository(repositoryPath)),
	fStats(std::make_unique<StatsLoader>(repositoryPath, BMessenger(this)))
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
	fList->AddColumn(new StatsColumn("Added", 60, StatsColumn::Kind::Added, *fStats), kAddedColumn);
	fList->AddColumn(new StatsColumn("Deleted", 60, StatsColumn::Kind::Deleted, *fStats), kDeletedColumn);
	fList->AddColumn(new StatsColumn("Files", 45, StatsColumn::Kind::Files, *fStats), kFilesColumn);
	fList->AddColumn(new BStringColumn("Author", 120, 40, 400, B_TRUNCATE_END), kAuthorColumn);
	fList->AddColumn(new BStringColumn("Date", 120, 40, 200, B_TRUNCATE_END), kDateColumn);
	fList->SetSortingEnabled(false);
	fList->SetSelectionMessage(new BMessage(kMsgCommitSelected));
	fList->SetInvocationMessage(new BMessage(kMsgCommitInvoked));

	fStatus = new BStringView("status", "");
	fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fShowChanges = new BButton("Changes" B_UTF8_ELLIPSIS, new BMessage(kMsgShowCommit));
	fRemove = new BButton("Remove", new BMessage(kMsgRemoveSteps));
	fCombine = new BButton("Combine", new BMessage(kMsgCombineStep));
	fReword = new BButton("Reword" B_UTF8_ELLIPSIS, new BMessage(kMsgRewordStep));
	fEditContents = new BButton("Edit" B_UTF8_ELLIPSIS, new BMessage(kMsgEditContents));
	fSplit = new BButton("Split" B_UTF8_ELLIPSIS, new BMessage(kMsgSplitStep));
	fResolve = new BButton("Resolve" B_UTF8_ELLIPSIS, new BMessage(kMsgResolveStep));
	fCombine->SetToolTip("Combine the commit with the older one before it");
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
			.Add(fEditContents)
			.Add(fSplit)
			.Add(fResolve)
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
	fList->SetDropLimit(fEdit != nullptr ? static_cast<int32>(fEdit->Steps().size()) : -1);

	fStats->Clear();
	auto addRow = [this](const git_oid &commit, int32 step, const std::string &state, const std::string &summary,
		const git_oid &statsCommit) {
		CommitRow *row = new CommitRow(commit, step, statsCommit);
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
		for (int32 column : {kAddedColumn, kDeletedColumn, kFilesColumn}) {
			row->SetField(new StatsField(statsCommit), column);
		}
		fList->AddRow(row);
		return row;
	};

	std::vector<CommitRow*> rows;
	if (fEdit != nullptr) {
		const std::vector<EditStep> &steps = fEdit->Steps();
		const std::vector<StepResult> &results = fEdit->Results();
		for (size_t i = steps.size(); i-- > 0;) {
			const git_oid statsCommit = results[i].state == StepState::Rewritten ? results[i].commit : steps[i].commit;
			rows.push_back(addRow(steps[i].commit, static_cast<int32>(i), StateText(steps[i], results[i]),
				FirstLine(fEdit->Message(i)), statsCommit));
		}
		if (fEdit->Base().has_value()) {
			rows.push_back(addRow(*fEdit->Base(), -1, "not editable", std::string(), *fEdit->Base()));
		}
	} else {
		for (const CommitInfo &commit : fCommits) {
			rows.push_back(addRow(commit.id, -1, std::string(), std::string(), commit.id));
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
	const StepResult *result = editing && steps.size() == 1 ? &fEdit->Results()[steps[0]] : nullptr;
	const bool replayed = result != nullptr && result->state != StepState::Conflict
		&& result->state != StepState::Pending;
	fEditContents->SetEnabled(replayed);
	fSplit->SetEnabled(replayed && result->state != StepState::Empty);
	fResolve->SetEnabled(result != nullptr && result->state == StepState::Conflict && !result->conflictFiles.empty());
	fReset->SetEnabled(editing && fEdit->HasChanges());
	fApply->SetEnabled(editing && fEdit->HasChanges() && fEdit->Problem().empty());
	if (editing && steps.size() == 1) {
		fCombine->SetLabel(fEdit->Steps()[steps[0]].squash ? "Separate" : "Combine");
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
	// The rows show the newest step first; index is the row the commits go
	// above.
	const int32 index = std::clamp<int32>(message->GetInt32("index", 0), 0, static_cast<int32>(count));
	const size_t position = count - index;
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
	const std::vector<size_t> steps = SelectedSteps();
	if (fEdit == nullptr || steps.empty()) {
		return;
	}
	fEdit->Remove(steps);
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


void HistoryWindow::OpenStepEditor(StepEditMode mode)
{
	const std::vector<size_t> steps = SelectedSteps();
	if (fEdit == nullptr || steps.size() != 1) {
		return;
	}
	const StepResult &result = fEdit->Results()[steps[0]];
	StepEditorInput input;
	input.mode = mode;
	input.step = steps[0];
	input.result = result.commit;
	input.parent = result.parent;
	input.conflicts = result.conflictFiles;
	input.message = fEdit->Message(steps[0]);
	try {
		(new StepEditor(this, fRepositoryPath, input))->Show();
	} catch (const std::exception &e) {
		ShowError(e.what());
	}
}


void HistoryWindow::StepEdited(BMessage *message)
{
	const int32 step = message->GetInt32("step", -1);
	if (fEdit == nullptr || step < 0 || static_cast<size_t>(step) >= fEdit->Steps().size()) {
		return;
	}
	const StepEditMode mode = static_cast<StepEditMode>(message->GetInt32("mode", 0));
	const StepResult &result = fEdit->Results()[step];
	std::map<std::string, FileOverride> files;
	const char *path = nullptr;
	for (int32 i = 0; message->FindString("path", i, &path) == B_OK; i++) {
		FileOverride file;
		const void *data = nullptr;
		ssize_t size = 0;
		if (!message->GetBool("deleted", i, false) && message->FindData("blob", B_RAW_TYPE, i, &data, &size) == B_OK
			&& size == sizeof(git_oid)) {
			file.blob = *static_cast<const git_oid*>(data);
		}
		int32 fileMode = GIT_FILEMODE_BLOB;
		message->FindInt32("fileMode", i, &fileMode);
		file.mode = static_cast<uint32_t>(fileMode);
		if (mode == StepEditMode::Resolve) {
			for (const ConflictFile &conflict : result.conflictFiles) {
				if (conflict.path == path) {
					file.resolves = conflict;
				}
			}
		}
		files[path] = file;
	}
	if (mode == StepEditMode::Split) {
		fEdit->Split(step, files, message->GetString("message", ""));
	} else {
		fEdit->SetFiles(step, files);
	}
	FillList();
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


// Redraws the rows that show the commit's statistics.
void HistoryWindow::StatsReady(BMessage *message)
{
	const void *data = nullptr;
	ssize_t size = 0;
	if (message->FindData("commit", B_RAW_TYPE, &data, &size) != B_OK || size != sizeof(git_oid)) {
		return;
	}
	const git_oid *commit = static_cast<const git_oid*>(data);
	for (int32 i = 0; i < fList->CountRows(); i++) {
		CommitRow *row = dynamic_cast<CommitRow*>(fList->RowAt(i));
		if (row != nullptr && git_oid_equal(&row->statsCommit, commit)) {
			fList->InvalidateRow(row);
		}
	}
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
		case kMsgEditContents:
			OpenStepEditor(StepEditMode::Edit);
			break;
		case kMsgSplitStep:
			OpenStepEditor(StepEditMode::Split);
			break;
		case kMsgResolveStep:
			OpenStepEditor(StepEditMode::Resolve);
			break;
		case kMsgStepEdited:
			StepEdited(message);
			break;
		case kMsgStatsReady:
			StatsReady(message);
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
