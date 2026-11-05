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

Syntax highlighting is enabled per document; C and C++ are built in:

```cpp
CodeEdit::ModelLock lock(*model);
model->SetLanguage(CodeEdit::LanguageForPath(path));   // "c", "cpp" or nullptr
```

Parsing runs incrementally in a background thread. Colors come from a
`CodeEdit::Theme` (`CodeEditView::SetTheme()`); the default follows Pe.


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
| Alt + B, Alt + Shift + B | Expand / shrink selection by syntax (inside brackets first) |
| Alt + Up/Down | Previous / next function or type definition |
| Alt + . | Fold the selected lines, or fold/unfold the block at the caret |
| Alt + [ / Alt + ] | Remove / add one indent level on the selected lines |

Indentation uses tabs unless `EditorSettings::insertSpacesForTab` is set;
`indentSize` and `tabWidth` correspond to editorconfig's `indent_size` and
`tab_width`. Tab inserts a tab character; Enter copies the leading whitespace
of the current line.

Folding: the triangles in the gutter fold and unfold blocks; clicking the
`...` placeholder after a folded line unfolds it. Folds belong to each view.
The view also accepts `CodeEdit::kMsgToggleFold`, `kMsgFoldAll` and
`kMsgUnfoldAll` messages.

Word wrap (`EditorSettings::wordWrap`) wraps lines at the view width,
preferably after blanks and punctuation. Continuation rows are indented by the
line's leading whitespace plus `wrapIndent` cells. Up/Down move by display
row; Home/End go to the row edges first, then to the line edges. After a
resize, lines are re-wrapped in the background, visible ones first.

`CodeEditView::Outline()` returns the definitions (namespaces, classes,
structs, unions, enums, functions) as a tree with their text ranges, e.g. for
a navigation panel. `#pragma mark Name` and `// #pragma mark Name` at
declaration level group the following definitions into sections; an unnamed
`#pragma mark -` between definitions becomes a separator. With `SetTarget()`, the view sends `kMsgContentsChanged`
when the outline may have changed and `kMsgCaretMoved` when the caret moved.
The demo shows the outline in a side panel (View > Outline).

Line marks (bookmarks, breakpoints, an execution line, or host-defined kinds)
belong to the model (`TextModel::AddMark()`), so all views show them. Each
view draws them in the gutter and may tint the line background, as set per
kind by `CodeEditView::SetMarkStyle()`. Clicking the gutter outside the fold
markers sends `kMsgGutterClicked` with the line to the target;
`RevealLine()` scrolls to a line without moving the caret. The demo's Marks
menu toggles bookmarks and breakpoints and steps a fake execution line;
in the gutter, click toggles a breakpoint, Shift+click enables or disables
it, and the secondary button toggles a bookmark.

Mouse: double click selects a word, or the contents of a bracket pair when
clicking a bracket; triple click selects a line.

For side-by-side views such as diffs:

- `EditorSettings::readOnly` makes a view read-only; other views of the
  same model may still edit it.
- `SetDecorations()` adds host highlights: line backgrounds, text range
  backgrounds and gap lines between lines. They follow edits until replaced.
- `SetHostFolds()` folds line ranges for the host, next to the user's
  folds; the user can unfold them as usual.
- `LineTop()`, `LineAtY()` and `ScrollLineTo()` give the vertical position
  of lines, and `kMsgViewportChanged` tells the target when it changed.
- `SetModel()` switches a view to another model.


## Debugger

`CodeEditDebugger` is a small debugger built on the view. It runs GDB's
Debug Adapter Protocol interpreter (`gdb -i=dap`, GDB 14 or later with
Python) and shows:

- a control window: program, arguments, launch, attach to a running team,
  continue, pause, step over/into/out, stop
- source windows; breakpoints are toggled in the gutter (Shift+click
  enables or disables) or with Cmd+K; the execution line and the selected
  caller frame are marked
- floating windows (Windows menu): console with GDB commands, threads,
  stack (double click opens the source), variables, breakpoints

`CodeEditDebugger [program [arguments...]]` fills in the program.
`build/debugger/DebuggerSample` is a program to try it on.

Debug menu: Cmd+R continue, Cmd+P pause, Cmd+N step over, Cmd+I step into,
Cmd+T step out.


## Git diff

`CodeEditGitDiff [repository or file]` edits the staging area of a Git
repository with libgit2 (no `git` process). Like Git GUI, it lists unstaged
and staged changes; selecting a file shows a diff:

- unstaged: index (read-only) and working tree
- staged: HEAD (read-only) and index

The index side of the staged diff is an editable text: edits are written to
the index with Save. Between the panes, shapes connect the changed chunks;
their arrows stage or unstage a chunk, which writes the index at once.
Unchanged regions are folded, and the panes scroll together.

Changes menu: Cmd+T stages or unstages the chunk at the caret or the
selected lines, Cmd+R reverts them in the working tree buffer, Cmd+Shift+T
stages or unstages the whole file (also a double click in the list),
Cmd+Shift+Down/Up go to the next or previous change. Cmd+S saves the right
pane (writes the index in the staged diff), Cmd+Shift+S saves all.

Binary files, files with mixed line endings, conflicts, symbolic links,
submodules and files with filter attributes are shown read-only.

The two-pane diff view, its link map and the chunk diff live in `diffkit/`,
a static library for diff apps.


## Building

Requirements: `meson`, `ninja`, `python3`, `tree_sitter_devel`, `catch2_devel`
(tests), `libgit2_1.9_devel` and `libpcre2_devel` (Git diff; `libgit2.pc`
requires `libpcre2-8.pc`). The C and C++ grammars are Meson wraps; their
release tarballs are downloaded on the first `meson setup`.

```sh
meson setup build
ninja -C build
meson test -C build
```

Options: `-Ddemo=false`, `-Ddebugger=false`, `-Dgitdiff=false`,
`-Ddircompare=false`,
`-Dtests=false`.

The demo application is `build/demo/CodeEditDemo`.


## License

MIT, see [LICENSE](LICENSE).
