#pragma once

#include <Rect.h>


class BFilePanel;
class BWindow;


namespace DiffKit {


// A window opened from another one is centered on it (and kept on screen).
// Unrelated windows, as new documents, keep their own placement. Call
// before the window is shown, with the frame of the window that opens it.
void PlaceOver(BWindow *window, BRect openerFrame);

// The same for a file panel, each time before it is shown.
void PlacePanel(BFilePanel *panel, BRect openerFrame);

// A window's default button takes Enter before the focused view. With this,
// Enter in an editable CodeEditView or multi-line BTextView types a new line
// instead; Enter in text controls and other views still reaches the button.
// Call once, on the window's thread or before it runs.
void KeepEnterInEditors(BWindow *window);


} // DiffKit
