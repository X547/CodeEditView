#include "FileOptions.h"

#include <DataIO.h>
#include <Message.h>


namespace Workspace {


// Larger values are taken for damage.
static constexpr uint32_t kMaxWidth = 64;


std::string FlattenFileOptions(const FileOptions &options)
{
	BMessage message;
	if (options.language.has_value()) {
		message.AddString("language", options.language->c_str());
	}
	message.AddInt32("tabWidth", static_cast<int32>(options.tabWidth));
	message.AddInt32("indentSize", static_cast<int32>(options.indentSize));
	message.AddBool("insertSpaces", options.insertSpaces);

	std::string data(message.FlattenedSize(), '\0');
	if (message.Flatten(data.data(), data.size()) != B_OK) {
		return {};
	}
	return data;
}


std::optional<FileOptions> UnflattenFileOptions(std::string_view data)
{
	BMessage message;
	BMemoryIO stream(data.data(), data.size());
	if (data.empty() || message.Unflatten(&stream) != B_OK) {
		return std::nullopt;
	}
	FileOptions options;
	const char *language = nullptr;
	if (message.FindString("language", &language) == B_OK) {
		options.language = language;
	}
	const int32 tabWidth = message.GetInt32("tabWidth", 0);
	if (tabWidth > 0 && static_cast<uint32_t>(tabWidth) <= kMaxWidth) {
		options.tabWidth = static_cast<uint32_t>(tabWidth);
	}
	const int32 indentSize = message.GetInt32("indentSize", 0);
	if (indentSize > 0 && static_cast<uint32_t>(indentSize) <= kMaxWidth) {
		options.indentSize = static_cast<uint32_t>(indentSize);
	}
	options.insertSpaces = message.GetBool("insertSpaces", false);
	return options;
}


} // Workspace
