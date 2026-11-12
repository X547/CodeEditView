#pragma once

#include <string>

#include <GroupView.h>
#include <Message.h>
#include <Messenger.h>

#include <git2.h>


class BGridLayout;


namespace GitTool {


// To the target of a DetailsView: a commit link was clicked. "commit" (raw
// git_oid); "replace" (bool): Ctrl was held, to show it in place of the
// current one.
constexpr uint32 kMsgCommitLinkClicked = 'GTlk';


// A commit for a drag: kMsgDragCommits, as rows of history windows.
BMessage CommitDragMessage(const git_oid &id, const BMessenger &source);
// A person for a drag: to Tracker, it makes a People file (the negotiated
// drag protocol); to text fields, it is "Name <email>".
BMessage PersonDragMessage(const std::string &name, const std::string &email);
// Fills a file Tracker made for a person drag (B_COPY_TARGET reply).
void WritePersonFile(const BMessage &reply, const std::string &name, const std::string &email);


// Rows of a label and a value: a selectable text, a commit (a link that
// can be dragged) or a person (dragged to Tracker as a People file), and
// at the end an optional message.
class DetailsView: public BGroupView {
private:
	BGridLayout *fGrid {};
	BView *fMessage {};
	BMessenger fTarget;
	int32 fRows {};

	void AddRow(const std::string &label, BView *value);

public:
	explicit DetailsView(const char *name);

	// Gets kMsgCommitLinkClicked.
	void SetTarget(const BMessenger &target);

	void Clear();
	void AddText(const std::string &label, const std::string &text);
	// text describes the commit; a link unless it is the shown commit.
	void AddCommit(const std::string &label, const git_oid &id, const std::string &text, bool link = true);
	void AddPerson(const std::string &label, const std::string &name, const std::string &email,
		const std::string &note = std::string());
	void SetMessage(const std::string &text);
};


} // GitTool
