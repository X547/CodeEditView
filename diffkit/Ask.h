#pragma once

#include <functional>

#include <Message.h>
#include <SupportDefs.h>


class BAlert;
class BWindow;


namespace DiffKit {


// Asking without blocking: a dialog is modal to the window that opened it
// only (that window takes no input but keeps working), and its answer runs
// a callback on that window's thread. If the window closes first, its
// dialogs close and their callbacks are dropped.


// Shows the alert and calls done with the index of the button chosen. Call
// on the owner's thread. The alert deletes itself.
void Ask(BWindow *owner, BAlert *alert, std::function<void(int32 button)> done);

// For other dialogs: a message that, sent to the owner, runs done with it
// on the owner's thread, once. Call on the owner's thread.
BMessage AnswerMessage(BWindow *owner, std::function<void(BMessage *answer)> done);

// The same, for each time the message is sent, until ForgetAnswer() or the
// owner closes. ForgetAnswer() may be called on any thread.
BMessage RepeatedAnswerMessage(BWindow *owner, std::function<void(BMessage *answer)> done);
void ForgetAnswer(const BMessage &message);

// Makes a window modal to the owner and closes it when the owner closes.
// Call on the owner's thread, before the window is shown.
void AttachDialog(BWindow *owner, BWindow *dialog);


} // DiffKit
