#include "Network.h"

#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>

#include <optional>

#include "SshConfig.h"


namespace GitTool {


static std::string Home()
{
	const char *home = getenv("HOME");
	return home != nullptr ? home : "";
}


// State of the callbacks during one network operation.
class Callbacks {
private:
	NetworkHandler &fHandler;
	// The URL set from the SSH configuration, not to be resolved again.
	std::string fResolvedUrl;
	SshHost fSshHost;
	std::optional<std::vector<std::string>> fKeys;
	size_t fKeyAttempt {};
	bool fTriedUsername {};
	int fPasswordAttempts {};
	std::vector<std::string> fRejected;

	std::vector<std::string> KeyFiles();

	static int RemoteReady(git_remote *remote, int direction, void *data);
	static int Credentials(git_credential **out, const char *url, const char *userFromUrl, unsigned int allowed, void *data);
	static int CertificateCheck(git_cert *certificate, int valid, const char *host, void *data);
	static int SidebandProgress(const char *text, int length, void *data);
	static int TransferProgress(const git_indexer_progress *stats, void *data);
	static int PushTransferProgress(unsigned int current, unsigned int total, size_t bytes, void *data);
	static int PushUpdateReference(const char *refName, const char *status, void *data);

public:
	explicit Callbacks(NetworkHandler &handler): fHandler(handler) {}

	git_remote_callbacks Get();
	// Throws for a failed libgit2 call, saying "Cancelled" if the user
	// cancelled.
	void Check(int result, const char *what);
	const std::vector<std::string> &Rejected() const {return fRejected;}
};


// The IdentityFile keys of the SSH configuration, or else the default ones,
// as ssh does.
std::vector<std::string> Callbacks::KeyFiles()
{
	std::vector<std::string> files;
	if (!fSshHost.identityFiles.empty()) {
		for (const std::string &path : fSshHost.identityFiles) {
			if (access(path.c_str(), R_OK) == 0) {
				files.push_back(path);
			} else {
				fHandler.Log("Cannot read the key " + path);
			}
		}
		return files;
	}
	const std::string home = Home();
	if (home.empty()) {
		return files;
	}
	for (const char *directory : {"/config/settings/ssh/", "/.ssh/"}) {
		for (const char *name : {"id_ed25519", "id_ecdsa", "id_rsa"}) {
			const std::string path = home + directory + name;
			if (access(path.c_str(), R_OK) == 0) {
				files.push_back(path);
			}
		}
	}
	return files;
}


// libssh2 does not read the OpenSSH configuration, so the host alias of an
// SSH URL is replaced by what the configuration says before connecting.
int Callbacks::RemoteReady(git_remote *remote, int direction, void *data)
{
	Callbacks *self = static_cast<Callbacks*>(data);
	try {
		const char *pushUrl = git_remote_pushurl(remote);
		const bool push = direction == GIT_DIRECTION_PUSH && pushUrl != nullptr;
		const char *urlText = push ? pushUrl : git_remote_url(remote);
		if (urlText == nullptr || urlText == self->fResolvedUrl) {
			return 0;
		}
		const std::string url = urlText;
		const std::optional<SshUrl> parsed = SshUrl::Parse(url);
		if (!parsed.has_value()) {
			return 0;
		}
		const std::string home = Home();
		self->fSshHost = ResolveSshHost(SshConfigFiles(home), home, parsed->host, parsed->user, parsed->port);
		self->fKeys.reset();
		self->fKeyAttempt = 0;
		if (!self->fSshHost.proxy.empty()) {
			self->fHandler.Log("Not supported in the SSH configuration: " + self->fSshHost.proxy);
		}
		SshUrl resolved = *parsed;
		resolved.host = self->fSshHost.hostName;
		resolved.user = self->fSshHost.user;
		resolved.port = self->fSshHost.port;
		const std::string resolvedUrl = resolved.ToString();
		if (resolvedUrl == url) {
			return 0;
		}
		self->fHandler.Log("Using the SSH configuration of " + parsed->host + ": " + resolvedUrl);
		const int result = push
			? git_remote_set_instance_pushurl(remote, resolvedUrl.c_str())
			: git_remote_set_instance_url(remote, resolvedUrl.c_str());
		if (result < 0) {
			return result;
		}
		self->fResolvedUrl = resolvedUrl;
		return 0;
	} catch (const std::exception &error) {
		self->fHandler.Log(std::string("Cannot use the SSH configuration: ") + error.what());
		return 0;
	}
}


int Callbacks::Credentials(git_credential **out, const char *url, const char *userFromUrl, unsigned int allowed,
	void *data)
{
	Callbacks *self = static_cast<Callbacks*>(data);
	const char *user = userFromUrl != nullptr ? userFromUrl : "git";
	if ((allowed & GIT_CREDENTIAL_USERNAME) != 0 && !self->fTriedUsername) {
		self->fTriedUsername = true;
		return git_credential_username_new(out, user);
	}
	if ((allowed & GIT_CREDENTIAL_SSH_KEY) != 0) {
		// The agent first, then the key files without passphrase. With
		// IdentitiesOnly the agent could offer other keys, so it is skipped.
		if (!self->fKeys.has_value()) {
			self->fKeys = self->KeyFiles();
		}
		const std::vector<std::string> &keys = *self->fKeys;
		const bool useAgent = !(self->fSshHost.identitiesOnly && !self->fSshHost.identityFiles.empty());
		while (self->fKeyAttempt <= keys.size()) {
			const size_t attempt = self->fKeyAttempt++;
			if (attempt == 0) {
				if (useAgent && git_credential_ssh_key_from_agent(out, user) == 0) {
					return 0;
				}
				continue;
			}
			const std::string &key = keys[attempt - 1];
			const std::string publicKey = key + ".pub";
			const bool hasPublic = access(publicKey.c_str(), R_OK) == 0;
			if (git_credential_ssh_key_new(out, user, hasPublic ? publicKey.c_str() : nullptr, key.c_str(), nullptr) == 0) {
				self->fHandler.Log("Trying the key " + key);
				return 0;
			}
		}
	}
	if ((allowed & GIT_CREDENTIAL_USERPASS_PLAINTEXT) != 0 && self->fPasswordAttempts < 3) {
		self->fPasswordAttempts++;
		std::string name = userFromUrl != nullptr ? userFromUrl : "";
		std::string password;
		if (!self->fHandler.AskPassword(url, name, password)) {
			git_error_set_str(GIT_ERROR_CALLBACK, "Cancelled");
			return GIT_EUSER;
		}
		return git_credential_userpass_plaintext_new(out, name.c_str(), password.c_str());
	}
	git_error_set_str(GIT_ERROR_CALLBACK, "No more credentials to try");
	return GIT_EAUTH;
}


static std::string HexBytes(const unsigned char *bytes, size_t count)
{
	static const char kDigits[] = "0123456789abcdef";
	std::string text;
	for (size_t i = 0; i < count; i++) {
		if (i > 0) {
			text += ':';
		}
		text += kDigits[bytes[i] >> 4];
		text += kDigits[bytes[i] & 15];
	}
	return text;
}


int Callbacks::CertificateCheck(git_cert *certificate, int valid, const char *host, void *data)
{
	if (valid != 0) {
		return 0;
	}
	Callbacks *self = static_cast<Callbacks*>(data);
	std::string details = "The identity of the server cannot be verified.";
	if (certificate->cert_type == GIT_CERT_HOSTKEY_LIBSSH2) {
		const git_cert_hostkey *hostKey = reinterpret_cast<const git_cert_hostkey*>(certificate);
		if ((hostKey->type & GIT_CERT_SSH_SHA256) != 0) {
			details += "\nSSH host key SHA-256: " + HexBytes(hostKey->hash_sha256, sizeof(hostKey->hash_sha256));
		}
	}
	if (self->fHandler.ConfirmServer(host != nullptr ? host : "", details)) {
		return 0;
	}
	git_error_set_str(GIT_ERROR_CALLBACK, "The server was not trusted");
	return GIT_ECERTIFICATE;
}


// Server lines worth keeping in the log, not only as progress: links (as
// Gerrit's to new changes), errors and warnings.
static bool IsServerMessage(const std::string &line)
{
	std::string lower;
	for (char c : line) {
		lower += static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return lower.find("://") != std::string::npos || lower.starts_with("error") || lower.starts_with("warning")
		|| lower.starts_with("hint") || lower.starts_with("fatal");
}


int Callbacks::SidebandProgress(const char *text, int length, void *data)
{
	Callbacks *self = static_cast<Callbacks*>(data);
	const std::string chunk(text, length);
	size_t start = 0;
	while (start < chunk.size()) {
		size_t end = chunk.find('\n', start);
		if (end == std::string::npos) {
			end = chunk.size();
		}
		std::string line = chunk.substr(start, end - start);
		start = end + 1;
		while (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		const size_t carriageReturn = line.rfind('\r');
		if (carriageReturn != std::string::npos) {
			line.erase(0, carriageReturn + 1);
		}
		const size_t first = line.find_first_not_of(' ');
		if (first == std::string::npos) {
			continue;
		}
		if (IsServerMessage(line.substr(first))) {
			self->fHandler.Log("Server: " + line.substr(first));
		} else {
			self->fHandler.Progress("Server: " + line);
		}
	}
	return self->fHandler.IsCancelled() ? -1 : 0;
}


int Callbacks::TransferProgress(const git_indexer_progress *stats, void *data)
{
	Callbacks *self = static_cast<Callbacks*>(data);
	std::string text = "Receiving objects: " + std::to_string(stats->received_objects) + "/"
		+ std::to_string(stats->total_objects) + ", " + std::to_string(stats->received_bytes / 1024) + " KiB";
	if (stats->total_deltas > 0) {
		text += "; resolving deltas: " + std::to_string(stats->indexed_deltas) + "/" + std::to_string(stats->total_deltas);
	}
	self->fHandler.Progress(text);
	return self->fHandler.IsCancelled() ? -1 : 0;
}


int Callbacks::PushTransferProgress(unsigned int current, unsigned int total, size_t bytes, void *data)
{
	Callbacks *self = static_cast<Callbacks*>(data);
	self->fHandler.Progress("Sending objects: " + std::to_string(current) + "/" + std::to_string(total) + ", "
		+ std::to_string(bytes / 1024) + " KiB");
	return self->fHandler.IsCancelled() ? -1 : 0;
}


int Callbacks::PushUpdateReference(const char *refName, const char *status, void *data)
{
	Callbacks *self = static_cast<Callbacks*>(data);
	if (status != nullptr) {
		self->fRejected.push_back(std::string(refName) + ": " + status);
	} else {
		self->fHandler.Log(std::string("Updated ") + refName);
	}
	return 0;
}


git_remote_callbacks Callbacks::Get()
{
	git_remote_callbacks callbacks = GIT_REMOTE_CALLBACKS_INIT;
	callbacks.remote_ready = RemoteReady;
	callbacks.credentials = Credentials;
	callbacks.certificate_check = CertificateCheck;
	callbacks.sideband_progress = SidebandProgress;
	callbacks.transfer_progress = TransferProgress;
	callbacks.push_transfer_progress = PushTransferProgress;
	callbacks.push_update_reference = PushUpdateReference;
	callbacks.payload = this;
	return callbacks;
}


void Callbacks::Check(int result, const char *what)
{
	if (result >= 0) {
		return;
	}
	if (fHandler.IsCancelled()) {
		throw GitError("Cancelled");
	}
	DiffKit::Check(result, what);
}


static RemotePtr LookupRemote(git_repository *repository, const std::string &name)
{
	git_remote *remote = nullptr;
	DiffKit::Check(git_remote_lookup(&remote, repository, name.c_str()), "Cannot find the remote");
	return RemotePtr(remote);
}


void Download(git_repository *repository, const std::string &remote, bool prune, bool allTags, NetworkHandler &handler)
{
	RemotePtr remotePtr = LookupRemote(repository, remote);
	Callbacks callbacks(handler);
	git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
	options.callbacks = callbacks.Get();
	options.prune = prune ? GIT_FETCH_PRUNE : GIT_FETCH_PRUNE_UNSPECIFIED;
	options.download_tags = allTags ? GIT_REMOTE_DOWNLOAD_TAGS_ALL : GIT_REMOTE_DOWNLOAD_TAGS_UNSPECIFIED;
	handler.Log("Downloading from " + remote);
	callbacks.Check(git_remote_fetch(remotePtr.get(), nullptr, &options, "download"), "Cannot download");
	const git_indexer_progress *stats = git_remote_stats(remotePtr.get());
	handler.Log("Downloaded " + std::to_string(stats->received_objects) + " objects from " + remote);
}


void DownloadRefspecs(git_repository *repository, const std::string &remote, const std::vector<std::string> &refspecs,
	NetworkHandler &handler)
{
	RemotePtr remotePtr = LookupRemote(repository, remote);
	Callbacks callbacks(handler);
	git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
	options.callbacks = callbacks.Get();
	options.download_tags = GIT_REMOTE_DOWNLOAD_TAGS_NONE;
	std::vector<char*> pointers;
	for (const std::string &refspec : refspecs) {
		pointers.push_back(const_cast<char*>(refspec.c_str()));
		handler.Log("Downloading " + refspec + " from " + remote);
	}
	const git_strarray array = {pointers.data(), pointers.size()};
	callbacks.Check(git_remote_fetch(remotePtr.get(), &array, &options, "download"), "Cannot download");
}


void Upload(git_repository *repository, const std::string &remote, const std::vector<std::string> &refspecs,
	NetworkHandler &handler)
{
	RemotePtr remotePtr = LookupRemote(repository, remote);
	Callbacks callbacks(handler);
	git_push_options options = GIT_PUSH_OPTIONS_INIT;
	options.callbacks = callbacks.Get();
	std::vector<char*> pointers;
	for (const std::string &refspec : refspecs) {
		pointers.push_back(const_cast<char*>(refspec.c_str()));
		handler.Log("Uploading " + refspec + " to " + remote);
	}
	const git_strarray array = {pointers.data(), pointers.size()};
	callbacks.Check(git_remote_push(remotePtr.get(), &array, &options), "Cannot upload");
	if (!callbacks.Rejected().empty()) {
		std::string message = "The server rejected:";
		for (const std::string &rejected : callbacks.Rejected()) {
			message += "\n" + rejected;
		}
		throw GitError(message);
	}
}


std::vector<ServerRef> ListServer(git_repository *repository, const std::string &remote, NetworkHandler &handler)
{
	RemotePtr remotePtr = LookupRemote(repository, remote);
	Callbacks callbacks(handler);
	const git_remote_callbacks remoteCallbacks = callbacks.Get();
	handler.Log("Connecting to " + remote);
	callbacks.Check(git_remote_connect(remotePtr.get(), GIT_DIRECTION_FETCH, &remoteCallbacks, nullptr, nullptr),
		"Cannot connect");
	const git_remote_head **heads = nullptr;
	size_t count = 0;
	callbacks.Check(git_remote_ls(&heads, &count, remotePtr.get()), "Cannot list the server references");
	std::vector<ServerRef> refs;
	for (size_t i = 0; i < count; i++) {
		ServerRef ref;
		ref.name = heads[i]->name;
		ref.id = heads[i]->oid;
		refs.push_back(std::move(ref));
	}
	git_remote_disconnect(remotePtr.get());
	return refs;
}


} // GitTool
