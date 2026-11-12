#include "Progress.h"

#include <exception>
#include <memory>
#include <thread>
#include <utility>

#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <Window.h>

#include "Ask.h"
#include "TruncatingLabel.h"
#include "WindowHelpers.h"


namespace DiffKit {


static constexpr uint32 kMsgProgressText = 'DKpt';
static constexpr uint32 kMsgProgressCancel = 'DKpc';
static constexpr uint32 kMsgProgressFinished = 'DKpf';


Progress::Progress(const BMessenger &dialog):
	fDialog(dialog)
{
}


void Progress::SetText(const std::string &text)
{
	BMessage message(kMsgProgressText);
	message.AddString("text", text.c_str());
	fDialog.SendMessage(&message);
}


//#pragma mark - ProgressDialog

// Closed by the work finishing, or by its owner closing; not by the user.
class ProgressDialog: public BWindow {
private:
	std::shared_ptr<Progress> fProgress;
	TruncatingLabel *fText {};
	BButton *fCancel {};

public:
	ProgressDialog(const std::string &title, bool cancellable);
	// Closed before the work ends (by its owner closing): the work is told
	// to stop.
	~ProgressDialog() override;

	const std::shared_ptr<Progress> &GetProgress() const {return fProgress;}

	void MessageReceived(BMessage *message) final;
};


ProgressDialog::ProgressDialog(const std::string &title, bool cancellable):
	BWindow(
		BRect(0, 0, 100, 100),
		title.c_str(),
		B_TITLED_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL,
		B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS
			| B_AUTO_UPDATE_SIZE_LIMITS
	),
	fProgress(std::make_shared<Progress>(BMessenger(this)))
{
	BStringView *heading = new BStringView("heading", (title + B_UTF8_ELLIPSIS).c_str());
	heading->SetFont(be_bold_font);
	heading->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	fText = new TruncatingLabel("text", "");
	fText->SetExplicitMinSize(BSize(360, B_SIZE_UNSET));

	BGroupLayout *layout = BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(heading)
		.Add(fText);
	if (cancellable) {
		fCancel = new BButton("Cancel", new BMessage(kMsgProgressCancel));
		layout->AddItem(BLayoutBuilder::Group<>(B_HORIZONTAL)
			.AddGlue()
			.Add(fCancel)
		);
	}
}


ProgressDialog::~ProgressDialog()
{
	fProgress->fCancelled = true;
}


void ProgressDialog::MessageReceived(BMessage *message)
{
	switch (message->what) {
		case kMsgProgressText:
			if (!fProgress->Cancelled()) {
				fText->SetLabel(message->GetString("text", ""));
			}
			break;
		case kMsgProgressCancel:
			fProgress->fCancelled = true;
			if (fCancel != nullptr) {
				fCancel->SetEnabled(false);
			}
			fText->SetLabel("Cancelling" B_UTF8_ELLIPSIS);
			break;
		case kMsgProgressFinished:
			Quit();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


//#pragma mark - RunWithProgress

// Runs work on a thread of its own, then sends answer to owner with
// "error".
static void StartWork(BWindow *owner, std::function<void()> work, BMessage answer)
{
	const BMessenger ownerMessenger(owner);
	std::thread([work = std::move(work), answer = std::move(answer), ownerMessenger]() mutable {
		std::string error;
		try {
			work();
		} catch (const std::exception &e) {
			error = e.what();
			if (error.empty()) {
				error = "Failed";
			}
		}
		answer.AddString("error", error.c_str());
		ownerMessenger.SendMessage(&answer);
	}).detach();
}


void RunWithProgress(
	BWindow *owner,
	const std::string &title,
	std::function<void(Progress &progress)> work,
	std::function<void(const std::string &error)> done,
	bool cancellable
)
{
	if (owner->Feel() != B_NORMAL_WINDOW_FEEL) {
		debugger("RunWithProgress() called with a dialog as the owner");
	}
	ProgressDialog *dialog = new ProgressDialog(title, cancellable);
	const std::shared_ptr<Progress> progress = dialog->GetProgress();
	const BMessenger dialogMessenger(dialog);
	BMessage answer = AnswerMessage(owner, [dialogMessenger, done = std::move(done)](BMessage *message) {
		dialogMessenger.SendMessage(kMsgProgressFinished);
		done(message->GetString("error", ""));
	});
	AttachDialog(owner, dialog);
	dialog->ResizeToPreferred();
	PlaceOver(dialog, owner->Frame());
	dialog->Show();
	StartWork(owner, [progress, work = std::move(work)]() {
		work(*progress);
	}, std::move(answer));
}


void RunInBackground(
	BWindow *owner,
	std::function<void()> work,
	std::function<void(const std::string &error)> done
)
{
	BMessage answer = AnswerMessage(owner, [done = std::move(done)](BMessage *message) {
		done(message->GetString("error", ""));
	});
	StartWork(owner, std::move(work), std::move(answer));
}


} // DiffKit
