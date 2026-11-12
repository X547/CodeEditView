#pragma once

#include <stdint.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Locker.h>
#include <Looper.h>
#include <Messenger.h>

#include <CodeEditView/TextModel.h>

#include "DapClient.h"
#include "Document.h"
#include "Json.h"
#include "PathMapper.h"
#include "Protocol.h"


namespace Debugger {


// A debugging session with GDB over DAP. Runs in its own thread; windows
// send it the commands of Protocol.h and receive notices with copies of the
// state they show. Breakpoints are the breakpoint marks shared by the
// documents of the registry, so every session has the same; GDB's paths
// map to the documents' locators by the session's PathMapper. The
// execution line and the selected frame are marks of the session's own
// mark sets (MarkSetFor()), shown by the views following the session.
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
		BReference<CodeEdit::TextModel> model;
		CodeEdit::ObserverId observer {};
		// The path GDB knows the file by.
		std::string path;
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
		BReference<CodeEdit::MarkSet> set;
		CodeEdit::MarkId id {};
	};

	Workspace::DocumentRegistry &fRegistry;
	const int32 fId;
	const Workspace::PathMapper fMapper;
	BLocker fMarkLock;
	std::map<CodeEdit::TextModel*, BReference<CodeEdit::MarkSet>> fMarkSets;

	std::unique_ptr<DapClient> fClient;
	SessionState fState = SessionState::Idle;
	std::string fStatus = "Not running";
	bool fAttached {};
	bool fInitialized {};
	bool fStartSent {};
	bool fConfigured {};
	// Breakpoints wait until attaching finished (see Start()).
	bool fBreakpointsHeld {};
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

	std::map<Workspace::Locator, WatchedDocument> fDocuments;
	// By GDB path.
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

	BReference<Workspace::Document> OpenDocument(const std::string &path);
	void WatchDocuments();
	void DocumentChanged(CodeEdit::TextModel *model);
	std::vector<int32> EnabledBreakpointLines(CodeEdit::TextModel &model);
	void SyncBreakpoints(bool force);
	void SendBreakpoints(const std::string &path, std::vector<int32> lines);
	void PlaceMark(std::optional<PlacedMark> &mark, uint32_t kind, const std::string &path, int32 line);
	void RemoveMark(std::optional<PlacedMark> &mark);
	void DropUnusedMarkSets();

	void Broadcast(BMessage &message);
	BMessage StateNotice() const;
	BMessage ThreadsNotice() const;
	BMessage StackNotice() const;
	BMessage ScopesNotice() const;
	BMessage BreakpointsNotice();

public:
	// The id goes with kMsgShowSource to the application.
	explicit DebugSession(Workspace::DocumentRegistry &registry, int32 id = 0,
		Workspace::PathMapper mapper = Workspace::PathMapper());
	~DebugSession() override;

	int32 Id() const {return fId;}
	// The marks of the session in a model, for views following the session.
	// Thread safe; must not be called with the model locked.
	BReference<CodeEdit::MarkSet> MarkSetFor(CodeEdit::TextModel *model);

	void MessageReceived(BMessage *message) override;
	bool QuitRequested() override;
};


} // Debugger
