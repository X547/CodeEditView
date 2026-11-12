#pragma once

#include <SupportDefs.h>


class BMessage;


namespace DiffKit {


// How other applications show files in CodeEdit.

constexpr const char *kCodeEditSignature = "application/x-vnd.CodeEdit";

// To CodeEdit: shows a file of a Git commit. "repository" (string: the top
// directory of the repository), "commit" (string: a commit id or revision
// name), "path" (string: relative to the top directory); optional "line"
// and "column" (int32, 1-based), where the caret goes.
constexpr uint32 kMsgCodeEditOpenGitFile = 'CEgf';

// Delivers the message to the running CodeEdit, else launches it with the
// message: the installed application (by signature), else the executable
// beside this program's or in the "studio" directory of the build tree
// beside it. Returns at once; the work runs on a thread of its own, and
// failures are shown in an alert.
void SendToCodeEdit(const BMessage &message);


} // DiffKit
