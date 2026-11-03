# CodeEditView

A monospace source code editor view (`BView`) for Haiku.

Planned features:

- monospace grid layout with half-width and CJK full-width characters
- syntax highlighting and navigation with TreeSitter
- multiple views of one document, live-updated across windows
- optional word wrap with continuation indent
- folding
- line marks (bookmarks, breakpoints, debugger execution line)
- Haiku text editing conventions (drag & drop, keyboard navigation, auto-scroll)


## Usage

```cpp
#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>

// A new empty document:
CodeEditView *editView = new CodeEditView("editor");
window->AddChild(new CodeEditScrollView("scroll", editView));

// Another view of the same document, possibly in another window:
CodeEditView *secondView = new CodeEditView("editor2", editView->Model());
```

`CodeEdit::TextModel` holds the text, undo history and line marks. Lock it with
`CodeEdit::ModelLock` before calling its methods. `Load()` and `Save()` read and
write a `BPositionIO`; line endings and a UTF-8 BOM are preserved.

The view handles `B_CUT`, `B_COPY`, `B_PASTE`, `B_SELECT_ALL`, `B_UNDO` and
`B_REDO` messages, so menu items can target it directly.


## Keyboard

Alt is the Command key, Win is the Option key.

| Keys | Action |
|---|---|
| Arrows | Move by character / line |
| Alt + Left/Right | Move by word |
| Win + Left/Right | Move by sub-word (camelCase, underscores) |
| Home / End | Line start (toggles first non-blank) / line end |
| Alt + Home/End | Start / end of file |
| Page Up/Down | Move to page edge, then by a page |
| Ctrl + arrows, Page Up/Down, Home/End | Scroll without moving the caret |
| Shift + any movement | Extend the selection |
| Ctrl + Backspace / Delete | Delete to line start / end |
| Alt + Z, Alt + Shift + Z | Undo, redo |


## Building

Requirements: `meson`, `ninja`, `tree_sitter_devel`, `catch2_devel` (tests).

```sh
meson setup build
ninja -C build
meson test -C build
```

Options: `-Ddemo=false`, `-Dtests=false`.

The demo application is `build/demo/CodeEditDemo`.


## License

MIT, see [LICENSE](LICENSE).
