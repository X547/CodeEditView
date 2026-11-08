#include "VariablesWindow.h"

#include <ColumnListView.h>
#include <ColumnTypes.h>
#include <LayoutBuilder.h>


namespace Debugger {


static constexpr uint32 kMsgVariableSelected = 'VWse';
static constexpr uint32 kMsgVariableInvoked = 'VWiv';

enum {
	kNameColumn,
	kValueColumn,
	kTypeColumn,
};


// A variable or scope. Rows with children get a placeholder child until
// they are loaded.
class VariableRow: public BRow {
public:
	const std::string path;
	const int64_t reference;
	bool loaded {};
	BRow *placeholder {};

	VariableRow(std::string path, int64_t reference):
		path(std::move(path)),
		reference(reference)
	{
	}
};


class ValueField: public BStringField {
public:
	const bool changed;

	ValueField(const char *value, bool changed):
		BStringField(value),
		changed(changed)
	{
	}
};


class ValueColumn: public BStringColumn {
public:
	ValueColumn():
		BStringColumn("Value", 200, 40, 4000, B_TRUNCATE_END)
	{
	}

	void DrawField(BField *field, BRect rect, BView *parent) override
	{
		const ValueField *value = dynamic_cast<ValueField*>(field);
		if (value != nullptr && value->changed) {
			parent->SetHighColor(220, 40, 40);
		}
		BStringColumn::DrawField(field, rect, parent);
	}
};


class VariablesList: public BColumnListView {
private:
	VariablesWindow *fWindow;

public:
	explicit VariablesList(VariablesWindow *window):
		BColumnListView("variables", B_NAVIGABLE, B_NO_BORDER, true),
		fWindow(window)
	{
	}

	void ExpandOrCollapse(BRow *row, bool expand) override
	{
		BColumnListView::ExpandOrCollapse(row, expand);
		fWindow->RowExpanded(row, expand);
	}
};


VariablesWindow::VariablesWindow(const SessionContext &context):
	ToolWindow(BRect(980, 360, 1379, 759), "Variables", context),
	fList(new VariablesList(this))
{
	fList->SetSortingEnabled(false);
	fList->SetSelectionMode(B_SINGLE_SELECTION_LIST);
	fList->SetSelectionMessage(new BMessage(kMsgVariableSelected));
	fList->SetInvocationMessage(new BMessage(kMsgVariableInvoked));
	fList->AddColumn(new BStringColumn("Name", 160, 40, 2000, B_TRUNCATE_END), kNameColumn);
	fList->AddColumn(new ValueColumn(), kValueColumn);
	fList->AddColumn(new BStringColumn("Type", 120, 30, 2000, B_TRUNCATE_END), kTypeColumn);
	fList->SetTarget(this);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.SetInsets(-1)
		.Add(fList);
	// Arguments and locals start expanded.
	fExpanded.insert("Arguments");
	fExpanded.insert("Locals");
}


BRow *VariablesWindow::AddRow(BRow *parent, const std::string &path, const char *name, const char *value,
	const char *type, int64_t reference)
{
	const auto previous = fPreviousValues.find(path);
	const bool changed = previous != fPreviousValues.end() && previous->second != value;
	fValues[path] = value;

	VariableRow *row = new VariableRow(path, reference);
	row->SetField(new BStringField(name), kNameColumn);
	row->SetField(new ValueField(value, changed), kValueColumn);
	row->SetField(new BStringField(type), kTypeColumn);
	fList->AddRow(row, parent);
	if (reference > 0) {
		row->placeholder = new BRow();
		row->placeholder->SetField(new BStringField(B_UTF8_ELLIPSIS), kNameColumn);
		fList->AddRow(row->placeholder, row);
		if (fExpanded.count(path) != 0) {
			fList->ExpandOrCollapse(row, true);
		}
	}
	return row;
}


// A new frame: the previous values are compared with the new ones.
void VariablesWindow::SetScopes(const BMessage &notice)
{
	if (!fValues.empty()) {
		fPreviousValues = std::move(fValues);
	}
	fValues.clear();
	fLoading.clear();
	fList->Clear();
	const char *name;
	for (int32 i = 0; notice.FindString("name", i, &name) == B_OK; i++) {
		AddRow(nullptr, name, name, "", "", notice.GetInt64("reference", i, 0));
	}
}


void VariablesWindow::AddVariables(const BMessage &notice)
{
	const auto it = fLoading.find(notice.GetInt64("reference", 0));
	if (it == fLoading.end()) {
		return;
	}
	VariableRow *parent = static_cast<VariableRow*>(it->second);
	fLoading.erase(it);
	if (parent->placeholder != nullptr) {
		fList->RemoveRow(parent->placeholder);
		delete parent->placeholder;
		parent->placeholder = nullptr;
	}
	const char *name;
	for (int32 i = 0; notice.FindString("name", i, &name) == B_OK; i++) {
		AddRow(parent, parent->path + "/" + name, name, notice.GetString("value", i, ""),
			notice.GetString("type", i, ""), notice.GetInt64("child", i, 0));
	}
}


void VariablesWindow::RowExpanded(BRow *row, bool expanded)
{
	VariableRow *variable = dynamic_cast<VariableRow*>(row);
	if (variable == nullptr) {
		return;
	}
	if (!expanded) {
		fExpanded.erase(variable->path);
		return;
	}
	fExpanded.insert(variable->path);
	if (variable->loaded || variable->reference <= 0) {
		return;
	}
	variable->loaded = true;
	fLoading[variable->reference] = variable;
	BMessage request(kMsgRequestVariables);
	request.AddInt64("reference", variable->reference);
	Session().SendMessage(&request);
}


void VariablesWindow::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kNoticeScopes:
			SetScopes(*message);
			break;
		case kNoticeVariables:
			AddVariables(*message);
			break;
		case kMsgVariableSelected:
		case kMsgVariableInvoked:
		case kNoticeState:
		case kNoticeOutput:
		case kNoticeThreads:
		case kNoticeStack:
		case kNoticeBreakpoints:
			break;
		default:
			ToolWindow::MessageReceived(message);
			break;
	}
}


} // Debugger
