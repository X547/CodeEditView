#include "Provider.h"

#include "FileProvider.h"
#include "GitProvider.h"


namespace Workspace {


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
