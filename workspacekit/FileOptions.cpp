#include "FileOptions.h"

#include <algorithm>

#include <DataIO.h>
#include <Message.h>


namespace Workspace {


// Larger values are taken for damage.
static constexpr uint32_t kMaxWidth = 64;


static std::string Flatten(const BMessage &message)
{
	std::string data(message.FlattenedSize(), '\0');
	if (message.Flatten(data.data(), data.size()) != B_OK) {
		return {};
	}
	return data;
}


static bool Unflatten(std::string_view data, BMessage &message)
{
	BMemoryIO stream(data.data(), data.size());
	return !data.empty() && message.Unflatten(&stream) == B_OK;
}


std::string FlattenFileOptions(const FileOptions &options)
{
	BMessage message;
	if (options.language.has_value()) {
		message.AddString("language", options.language->c_str());
	}
	if (options.tabWidth.has_value()) {
		message.AddInt32("tabWidth", static_cast<int32>(*options.tabWidth));
	}
	if (options.indentSize.has_value()) {
		message.AddInt32("indentSize", static_cast<int32>(*options.indentSize));
	}
	if (options.insertSpaces.has_value()) {
		message.AddBool("insertSpaces", *options.insertSpaces);
	}
	message.AddBool("wordWrap", options.wordWrap);
	return Flatten(message);
}


std::optional<FileOptions> UnflattenFileOptions(std::string_view data)
{
	BMessage message;
	if (!Unflatten(data, message)) {
		return std::nullopt;
	}
	FileOptions options;
	const char *language = nullptr;
	if (message.FindString("language", &language) == B_OK) {
		options.language = language;
	}
	int32 tabWidth = 0;
	if (message.FindInt32("tabWidth", &tabWidth) == B_OK && tabWidth > 0
		&& static_cast<uint32_t>(tabWidth) <= kMaxWidth) {
		options.tabWidth = static_cast<uint32_t>(tabWidth);
	}
	int32 indentSize = 0;
	if (message.FindInt32("indentSize", &indentSize) == B_OK && indentSize >= 0
		&& static_cast<uint32_t>(indentSize) <= kMaxWidth) {
		options.indentSize = static_cast<uint32_t>(indentSize);
	}
	bool insertSpaces = false;
	if (message.FindBool("insertSpaces", &insertSpaces) == B_OK) {
		options.insertSpaces = insertSpaces;
	}
	options.wordWrap = message.GetBool("wordWrap", false);
	return options;
}


std::string FlattenBookmarks(const std::vector<size_t> &lines)
{
	BMessage message;
	for (const size_t line : lines) {
		message.AddInt64("line", static_cast<int64>(line));
	}
	return Flatten(message);
}


// Sorted, without duplicates.
std::vector<size_t> UnflattenBookmarks(std::string_view data)
{
	std::vector<size_t> lines;
	BMessage message;
	if (!Unflatten(data, message)) {
		return lines;
	}
	int64 line = 0;
	for (int32 i = 0; message.FindInt64("line", i, &line) == B_OK; i++) {
		if (line >= 0) {
			lines.push_back(static_cast<size_t>(line));
		}
	}
	std::sort(lines.begin(), lines.end());
	lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
	return lines;
}


} // Workspace
