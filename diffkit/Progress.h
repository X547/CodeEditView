#pragma once

#include <atomic>
#include <functional>
#include <string>

#include <Messenger.h>


class BWindow;


namespace DiffKit {


class ProgressDialog;


// What work running under a progress dialog reports, from its own thread.
class Progress {
private:
	BMessenger fDialog;
	std::atomic<bool> fCancelled {};

	friend class ProgressDialog;

public:
	explicit Progress(const BMessenger &dialog);

	// Shown under the title.
	void SetText(const std::string &text);
	// The user asked to cancel; the work stops when it sees this (by
	// returning or throwing).
	bool Cancelled() const {return fCancelled;}
	// For work that polls a flag of its own.
	const std::atomic<bool> &CancelFlag() const {return fCancelled;}
};


// Runs work on a thread of its own while a dialog with the title and the
// progress text is shown over the owner, which takes no input meanwhile
// but keeps working. Then the dialog closes and done runs on the owner's
// thread with the error (empty on success). If the owner closes first, the
// dialog closes, done is dropped, and the work is told to stop
// (Progress::Cancelled()) and finishes unseen.
//
// The work must not use the owner's objects: it captures copies (as a
// repository path, to open its own handle). The owner must not be modal
// itself: Haiku has no hierarchy of modal windows. Call on the owner's
// thread.
void RunWithProgress(
	BWindow *owner,
	const std::string &title,
	std::function<void(Progress &progress)> work,
	std::function<void(const std::string &error)> done,
	bool cancellable = false
);


} // DiffKit
