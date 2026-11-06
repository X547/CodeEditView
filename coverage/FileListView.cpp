#include "FileListView.h"

#include <stdio.h>

#include <optional>
#include <utility>

#include <ColumnTypes.h>

#include "Messages.h"


namespace Coverage {


enum {
	kFileColumn,
	kLinesColumn,
	kRegionsColumn,
	kBranchesColumn,
	kMissedColumn,
};


class FileRow: public BRow {
public:
	std::string path;

	explicit FileRow(std::string rowPath):
		path(std::move(rowPath))
	{
	}
};


class PercentField: public BField {
public:
	std::optional<double> percent;

	explicit PercentField(std::optional<double> value):
		percent(value)
	{
	}
};


// Percentages with one decimal; files without anything to cover sort first.
class PercentColumn: public BTitledColumn {
public:
	PercentColumn(const char *title, float width):
		BTitledColumn(title, width, 40, 200, B_ALIGN_RIGHT)
	{
	}

	void DrawField(BField *field, BRect rect, BView *parent) final
	{
		const PercentField *percentField = static_cast<PercentField*>(field);
		char text[32] = "-";
		if (percentField->percent.has_value()) {
			snprintf(text, sizeof(text), "%.1f %%", *percentField->percent);
		}
		DrawString(text, parent, rect);
	}

	int CompareFields(BField *field1, BField *field2) final
	{
		const double value1 = static_cast<PercentField*>(field1)->percent.value_or(-1);
		const double value2 = static_cast<PercentField*>(field2)->percent.value_or(-1);
		return value1 < value2 ? -1 : value1 > value2 ? 1 : 0;
	}

	bool AcceptsField(const BField *field) const final
	{
		return dynamic_cast<const PercentField*>(field) != nullptr;
	}
};


std::string CommonDirectory(const Summary &summary)
{
	if (summary.files.empty()) {
		return std::string();
	}
	std::string prefix = summary.files[0].path;
	for (const FileSummary &file: summary.files) {
		size_t length = 0;
		while (length < prefix.size() && length < file.path.size() && prefix[length] == file.path[length]) {
			length++;
		}
		prefix.resize(length);
	}
	const size_t slash = prefix.rfind('/');
	return slash == std::string::npos ? std::string() : prefix.substr(0, slash + 1);
}


FileListView::FileListView():
	BColumnListView("files", B_NAVIGABLE, B_PLAIN_BORDER, true)
{
	SetSelectionMode(B_SINGLE_SELECTION_LIST);
	AddColumn(new BStringColumn("File", 220, 60, 2000, B_TRUNCATE_BEGINNING), kFileColumn);
	AddColumn(new PercentColumn("Lines", 60), kLinesColumn);
	AddColumn(new PercentColumn("Regions", 60), kRegionsColumn);
	AddColumn(new PercentColumn("Branches", 70), kBranchesColumn);
	AddColumn(new BIntegerColumn("Missed lines", 70, 30, 200, B_ALIGN_RIGHT), kMissedColumn);
	SetSortColumn(ColumnAt(kFileColumn), false, true);
	SetSelectionMessage(new BMessage(kMsgFileSelected));
}


void FileListView::SetSummary(const Summary &summary)
{
	const std::string selected = SelectedPath();
	Clear();
	const std::string directory = CommonDirectory(summary);
	for (const FileSummary &file: summary.files) {
		FileRow *row = new FileRow(file.path);
		row->SetField(new BStringField(file.path.substr(directory.size()).c_str()), kFileColumn);
		row->SetField(new PercentField(file.lines.Percent()), kLinesColumn);
		row->SetField(new PercentField(file.regions.Percent()), kRegionsColumn);
		row->SetField(new PercentField(file.branches.Percent()), kBranchesColumn);
		row->SetField(new BIntegerField(static_cast<int32>(file.lines.count - file.lines.covered)), kMissedColumn);
		AddRow(row);
	}
	if (!selected.empty()) {
		Select(selected);
	}
}


std::string FileListView::SelectedPath()
{
	const FileRow *row = dynamic_cast<FileRow*>(CurrentSelection());
	return row != nullptr ? row->path : std::string();
}


void FileListView::Select(const std::string &path)
{
	for (int32 i = 0; i < CountRows(); i++) {
		FileRow *row = dynamic_cast<FileRow*>(RowAt(i));
		if (row != nullptr && row->path == path) {
			AddToSelection(row);
			ScrollTo(row);
			return;
		}
	}
}


} // Coverage
