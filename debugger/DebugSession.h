#pragma once

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Looper.h>
#include <Messenger.h>

#include <CodeEditView/TextModel.h>

#include "DapClient.h"
#include "Documents.h"
#include "Json.h"
#include "Protocol.h"


namespace Debugger {


// A debugging session with GDB over DAP. Runs in its own thread; windows
// send it the commands of Protocol.h and receive notices with copies of the
// state they show. Breakpoints are the breakpoint marks of the documents
// in the DocumentRegistry.
class DebugSession: public BLooper {
private:
	using ResponseHandler = std::function<void(const JsonValue &response)>;

	struct ThreadInfo {
		int64_t id {};
		std::string name;
	};

	struct FrameInfo {
		int64_t id {};
		std::string function;
		std::string module;
		std::string path;
		int32 line {};
	};

	struct ScopeInfo {
		std::string name;
		int64_t reference {};
	};

	struct WatchedDocument {
		BReference<SourceDocument> document;
		CodeEdit::ObserverId observer {};
		// Enabled breakpoint lines last sent to the adapter (1-based).
		std::optional<std::vector<int32>> sentLines;
	};

	struct BreakpointStatus {
		int32 requestedLine {};
		int64_t id {};
		bool verified {};
		std::string message;
	};

	struct PlacedMark {
		BReference<SourceDocument> document;
		CodeEdit::MarkId id {};
	};

	std::unique_ptr<DapClient> fClient;
	SessionState fState = SessionState::Idle;
	std::string fStatus = "Not running";
	bool fAttached {};
	bool fInitialized {};
	bool fStartSent {};
	bool fConfigured {};
	std::map<int64_t, std::pair<std::string, ResponseHandler>> fPending;
	std::vector<BMessenger> fWatchers;

	std::vector<ThreadInfo> fThreads;
	int64_t fCurrentThread {};
	std::vector<FrameInfo> fFrames;
	int32 fCurrentFrame {};
	std::vector<ScopeInfo> fScopes;
	// Counts stops; responses for an earlier stop are ignored.
	uint64_t fStopGeneration {};
	bool fThreadsRefreshPosted {};

	std::map<std::string, WatchedDocument> fDocuments;
	std::map<std::string, std::vector<BreakpointStatus>> fBreakpointStatus;
	bool fBreakpointSyncPosted {};
	std::optional<PlacedMark> fExecMark;
	std::optional<PlacedMark> fFrameMark;

	void Request(const std::string &command, JsonValue arguments, ResponseHandler handler = {});
	void HandleMessage(const std::string &json);
	void HandleEvent(const std::string &event, const JsonValue &body);

	void Start(const std::string &command, JsonValue arguments, std::vector<std::string> gdbArguments, const std::string &status);
	void MaybeConfigure();
	void End();
	void Resume(const char *command);
	void SetState(SessionState state, const std::string &status);
	void ClearStopState();
	void RefreshThreads();
	void RequestStack(bool showSource);
	void SelectFrame(int32 index, bool showSource);
	void RequestVariables(int64_t reference);
	void Output(const std::string &text, const char *category = "console");

	void WatchDocument(const std::string &path);
	void DocumentChanged(CodeEdit::TextModel *model);
	std::vector<int32> EnabledBreakpointLines(CodeEdit::TextModel &model);
	void SyncBreakpoints(bool force);
	void SendBreakpoints(const std::string &path, std::vector<int32> lines);
	void PlaceMark(std::optional<PlacedMark> &mark, uint32_t kind, const std::string &path, int32 line);
	void RemoveMark(std::optional<PlacedMark> &mark);

	void Broadcast(BMessage &message);
	BMessage StateNotice() const;
	BMessage ThreadsNotice() const;
	BMessage StackNotice() const;
	BMessage ScopesNotice() const;
	BMessage BreakpointsNotice();

public:
	DebugSession();
	~DebugSession() override;

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Debugger
