#include "Provider.h"

#include "FileProvider.h"
#include "GitProvider.h"


namespace Workspace {


//#pragma mark - Provider

FileData Provider::Read(const Locator &locator)
{
	class StringSink: public DataSink {
	public:
		std::string data;

		void Restart() final {data.clear();}
		void Append(std::string_view chunk) final {data.append(chunk);}
	};

	StringSink sink;
	const FileInfo info = Read(locator, sink);
	FileData result;
	if (info.exists) {
		result.data = std::move(sink.data);
	}
	result.version = info.version;
	result.mimeType = info.mimeType;
	return result;
}


std::string Provider::Write(const Locator &locator, std::string_view data, const std::string &expectedVersion,
	bool overwrite)
{
	return Write(
		locator,
		[data](const DiffKit::ChunkSink &sink) {
			sink(data);
		},
		expectedVersion,
		overwrite
	);
}


bool Provider::HasAttributes(const Locator &locator) const
{
	(void)locator;
	return false;
}


std::string Provider::WriteAttributes(const Locator &locator, const FileAttributes &attributes,
	const std::string &knownVersion)
{
	(void)attributes;
	(void)knownVersion;
	throw ProviderError("Files of " + locator.DisplayName() + " have no attributes");
}


//#pragma mark - Providers

Providers &Providers::Default()
{
	static Providers *providers = [] {
		Providers *result = new Providers();
		result->Add(LocatorKind::File, std::make_unique<FileProvider>());
		result->Add(LocatorKind::GitCommit, std::make_unique<GitProvider>());
		return result;
	}();
	return *providers;
}


void Providers::Add(LocatorKind kind, std::unique_ptr<Provider> provider)
{
	fProviders[kind] = std::move(provider);
}


Provider *Providers::Find(LocatorKind kind) const
{
	const auto it = fProviders.find(kind);
	return it != fProviders.end() ? it->second.get() : nullptr;
}


Provider &Providers::Get(const Locator &locator) const
{
	Provider *provider = Find(locator.Kind());
	if (provider == nullptr) {
		switch (locator.Kind()) {
			case LocatorKind::Sftp:
				throw ProviderError("SFTP is not supported yet");
			default:
				throw ProviderError("Unsupported location " + locator.ToString());
		}
	}
	return *provider;
}


} // Workspace
