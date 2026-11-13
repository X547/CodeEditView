#include "ToolBarMenus.h"

#include <algorithm>
#include <string_view>

#include <Autolock.h>
#include <Entry.h>
#include <Path.h>
#include <Roster.h>

#include "Messages.h"


namespace Studio {


using CodeEdit::ModelLock;
using CodeEdit::OutlineItem;
using CodeEdit::OutlineKind;
using CodeEdit::TextModel;


// Longer lines are not looked at for "#include".
static constexpr size_t kMaxDirectiveLength = 1024;


//#pragma mark - ModelMenu

ModelMenu::ModelMenu(const char *name, TextModel *model):
	LazyMenu(name),
	fLock("ModelMenu"),
	fModel(model)
{
}


BReference<TextModel> ModelMenu::Model() const
{
	BAutolock lock(fLock);
	return fModel;
}


void ModelMenu::SetModel(TextModel *model)
{
	BAutolock lock(fLock);
	fModel.SetTo(model);
}


//#pragma mark - DefinitionsMenu

DefinitionsMenu::DefinitionsMenu(const char *name, TextModel *model, const BMessage &message,
	const BMessenger &target):
	ModelMenu(name, model),
	fMessage(message),
	fTarget(target)
{
}


// As in Pe, a separator without a name gives way to any other separator,
// and the list does not start with one.
void DefinitionsMenu::AddSeparator(const std::string &label)
{
	const bool afterSeparator = !fEntries.empty() && fEntries.back().kind != EntryKind::Definition;
	if (label.empty()) {
		if (!fEntries.empty() && !afterSeparator) {
			fEntries.push_back({EntryKind::Separator, {}, 0, 0});
		}
		return;
	}
	if (afterSeparator && fEntries.back().kind == EntryKind::Separator) {
		fEntries.pop_back();
	}
	fEntries.push_back({EntryKind::Section, label, 0, 0});
}


void DefinitionsMenu::AddEntries(const std::vector<OutlineItem> &items, int32 depth)
{
	for (const OutlineItem &item : items) {
		switch (item.kind) {
			case OutlineKind::Separator:
				AddSeparator({});
				break;
			case OutlineKind::Section:
				AddSeparator(item.label);
				AddEntries(item.children, depth);
				break;
			case OutlineKind::Namespace:
				AddEntries(item.children, depth);
				break;
			default:
				fEntries.push_back({
					EntryKind::Definition,
					std::string(depth * 4, ' ') + item.label,
					item.nameStart,
					item.nameEnd
				});
				AddEntries(item.children, depth + 1);
				break;
		}
	}
}


size_t DefinitionsMenu::Prepare()
{
	std::vector<OutlineItem> outline;
	if (const BReference<TextModel> model = Model(); model.Get() != nullptr) {
		ModelLock lock(*model);
		outline = model->Outline();
	}
	fEntries.clear();
	AddEntries(outline, 0);
	while (!fEntries.empty() && fEntries.back().kind == EntryKind::Separator) {
		fEntries.pop_back();
	}
	if (fEntries.empty()) {
		fEntries.push_back({EntryKind::Nothing, "No definitions", 0, 0});
	}
	return fEntries.size();
}


BMenuItem *DefinitionsMenu::MakeItem(size_t index)
{
	const Entry &entry = fEntries[index];
	switch (entry.kind) {
		case EntryKind::Separator:
			return new BSeparatorItem();
		case EntryKind::Section:
			return new SectionItem(entry.label.c_str());
		case EntryKind::Nothing: {
			BMenuItem *item = new BMenuItem(entry.label.c_str(), nullptr);
			item->SetEnabled(false);
			return item;
		}
		default: {
			BMessage *message = new BMessage(fMessage);
			message->AddUInt64("start", entry.nameStart);
			message->AddUInt64("end", entry.nameEnd);
			BMenuItem *item = new BMenuItem(entry.label.c_str(), message);
			item->SetTarget(fTarget);
			return item;
		}
	}
}


void DefinitionsMenu::Release()
{
	fEntries = std::vector<Entry>();
}


//#pragma mark - Includes

static void ParseInclude(std::string_view line, std::vector<Include> &includes)
{
	size_t i = line.find_first_not_of(" \t");
	if (i == std::string_view::npos || line[i] != '#') {
		return;
	}
	i = line.find_first_not_of(" \t", i + 1);
	if (i == std::string_view::npos || line.substr(i, 7) != "include") {
		return;
	}
	i = line.find_first_not_of(" \t", i + 7);
	if (i == std::string_view::npos || (line[i] != '<' && line[i] != '"')) {
		return;
	}
	const bool system = line[i] == '<';
	const size_t end = line.find(system ? '>' : '"', i + 1);
	if (end == std::string_view::npos || end == i + 1) {
		return;
	}
	Include include {std::string(line.substr(i + 1, end - i - 1)), system};
	const bool known = std::any_of(includes.begin(), includes.end(), [&include](const Include &other) {
		return other.name == include.name && other.system == include.system;
	});
	if (!known) {
		includes.push_back(std::move(include));
	}
}


// Goes through the rope's chunks; only the start of each line is copied.
std::vector<Include> FindIncludes(const CodeEdit::Rope &text)
{
	std::vector<Include> includes;
	std::string line;
	size_t pos = 0;
	while (pos < text.Length()) {
		std::string_view chunk = text.ChunkAt(pos);
		pos += chunk.size();
		while (!chunk.empty()) {
			const size_t newline = chunk.find('\n');
			const std::string_view part = chunk.substr(0, newline);
			if (line.size() < kMaxDirectiveLength) {
				line.append(part.substr(0, kMaxDirectiveLength - line.size()));
			}
			if (newline == std::string_view::npos) {
				break;
			}
			ParseInclude(line, includes);
			line.clear();
			chunk.remove_prefix(newline + 1);
		}
	}
	ParseInclude(line, includes);
	return includes;
}


//#pragma mark - IncludesMenu

IncludesMenu::IncludesMenu(const char *name, TextModel *model, const BMessage &message, const BMessenger &target):
	ModelMenu(name, model),
	fMessage(message),
	fTarget(target)
{
}


// The includes are found in a snapshot of the text, outside of the lock.
size_t IncludesMenu::Prepare()
{
	CodeEdit::Rope text;
	if (const BReference<TextModel> model = Model(); model.Get() != nullptr) {
		ModelLock lock(*model);
		text = model->Text();
	}
	fIncludes = FindIncludes(text);
	return std::max<size_t>(fIncludes.size(), 1);
}


BMenuItem *IncludesMenu::MakeItem(size_t index)
{
	if (fIncludes.empty()) {
		BMenuItem *none = new BMenuItem("No included files", nullptr);
		none->SetEnabled(false);
		return none;
	}
	const Include &include = fIncludes[index];
	BMessage *message = new BMessage(fMessage);
	message->AddString("name", include.name.c_str());
	message->AddBool("system", include.system);
	BMenuItem *item = new BMenuItem(include.name.c_str(), message);
	item->SetTarget(fTarget);
	return item;
}


void IncludesMenu::Release()
{
	fIncludes = std::vector<Include>();
}


//#pragma mark - OpenMenu

OpenMenu::OpenMenu(const char *name, const BMessenger &target, const char *signature, int32 recentCount):
	LazyMenu(name),
	fTarget(target),
	fSignature(signature),
	fRecentCount(recentCount)
{
}


OpenMenu::~OpenMenu()
{
	Release();
}


size_t OpenMenu::Prepare()
{
	Release();
	fItems.push_back(new BMenuItem("Open" B_UTF8_ELLIPSIS, new BMessage(kMsgShowOpenFile)));
	fItems.push_back(new BMenuItem("Open from Git" B_UTF8_ELLIPSIS, new BMessage(kMsgShowOpenGit)));
	fItems.push_back(new BMenuItem("Open location" B_UTF8_ELLIPSIS, new BMessage(kMsgShowOpenLocation)));

	BMessage recent;
	be_roster->GetRecentDocuments(&recent, fRecentCount, nullptr, fSignature.c_str());
	entry_ref ref;
	for (int32 i = 0; recent.FindRef("refs", i, &ref) == B_OK; i++) {
		BPath path(&ref);
		if (path.InitCheck() != B_OK) {
			continue;
		}
		if (fItems.size() == 3) {
			fItems.push_back(new BSeparatorItem());
		}
		BMessage *open = new BMessage(B_REFS_RECEIVED);
		open->AddRef("refs", &ref);
		fItems.push_back(new BMenuItem(path.Path(), open));
	}
	for (BMenuItem *item : fItems) {
		item->SetTarget(fTarget);
	}
	return fItems.size();
}


BMenuItem *OpenMenu::MakeItem(size_t index)
{
	BMenuItem *item = fItems[index];
	fItems[index] = nullptr;
	return item;
}


void OpenMenu::Release()
{
	for (BMenuItem *item : fItems) {
		delete item;
	}
	fItems.clear();
}


} // Studio
