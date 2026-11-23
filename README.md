# CodeEditView

A source code editor view for Haiku, and the applications built on it.


## Applications

| Application | Executable | |
|---|---|---|
| [CodeEdit](studio/README.md) | `CodeEdit` | Source code editor with a GDB debugger and LLVM coverage display |
| [GitTool](gittool/README.md) | `CodeEditGitTool` | Git worktrees, branches, remotes, tags, staging and commits, history editing |
| [DirCompare](dircompare/README.md) | `CodeEditDirCompare` | Compare and merge two folders or two files |

They share one text editor, described in [Editing text](docs/Editing.md).

The other executables are demos and development tools, not meant for end
users:

- `CodeEditDemo`: the editor view with test menus (marks, folding, outline).
- `CodeEditDebugger`: the debugger of CodeEdit on its own.
- `CodeEditGitDiff`: the staging window of GitTool on its own.
- `CodeEditCoverage`: the coverage display of CodeEdit on its own.


## Building

Requirements: Meson with Haiku resource support (the `haiku-rdef` branch
of https://github.com/X547/meson; CodeEdit lists its `.rdef` file as a
source), `ninja`, `python3`, `tree_sitter_devel` (0.25 or later),
`libpcre2_devel`, `libgit2_1.9_devel` and `catch2_devel` (tests). Some
grammars are Meson wraps, downloaded on the first `meson setup`. At run
time, debugging needs GDB 14 or later with Python, and coverage needs
`llvm-cov`.

The Meson branch runs uninstalled: use `python3 <checkout>/meson.py` in
place of `meson` below.

```sh
meson setup build
ninja -C build
meson test -C build
```

To install the applications, for example for the current user:

```sh
meson setup build --prefix=$(finddir B_USER_NONPACKAGED_DIRECTORY)
meson install -C build
```

They are installed in `apps/CodeEditView` of the prefix, with the shared
library in its `lib` folder. Installing also registers the file types and
icons of CodeEdit.

Options (all on by default): `-Dstudio=false` (CodeEdit),
`-Dgittool=false`, `-Ddircompare=false`, `-Dgitdiff=false`,
`-Ddebugger=false`, `-Dcoverage=false`, `-Ddemo=false`, `-Dtests=false`.


## Source layout

| Directory | |
|---|---|
| `headers/CodeEditView`, `src` | The editor view and text model (`libcodeeditview`) |
| `findkit` | Find and replace: search engine (PCRE2), commands, find panel |
| `diffkit` | Two-pane diff view, chunk diff, shared window helpers |
| `workspacekit` | Documents addressed by locations (files, Git commits), their providers |
| `debugger` | Debug sessions over GDB's Debug Adapter Protocol, debugger windows |
| `coverage` | LLVM coverage reading and display |
| `studio` | CodeEdit |
| `gittool` | GitTool |
| `gitdiff` | The staging window of GitTool |
| `dircompare` | DirCompare |
| `demo` | CodeEditDemo |
| `tests`, `*/tests` | Catch2 tests |
| `tools` | Build helpers and checks |
| `subprojects` | TreeSitter grammars |


## Using the editor view

```cpp
#include <CodeEditView/CodeEditScrollView.h>
#include <CodeEditView/CodeEditView.h>

// A new empty document:
CodeEditView *editView = new CodeEditView("editor");
window->AddChild(new CodeEditScrollView("scroll", editView));

// Another view of the same document, possibly in another window:
CodeEditView *secondView = new CodeEditView("editor2", editView->Model());
```

**Text model.** `CodeEdit::TextModel` holds the text (a rope), the undo
history, line marks and the syntax tree, shared by all its views. Lock it with
`CodeEdit::ModelLock` before calling its methods. `Load()` and `Save()` read
and write a `BPositionIO`; line endings and a UTF-8 BOM are kept.

**Undo.** Edits made through an `UndoScript` are one named undo step (like
BlackBox's `BeginScript`/`EndScript`); scripts nest. The owner
(`CodeEditView::EditOwner()`) is the view whose selection undo restores.

```cpp
CodeEdit::ModelLock lock(*model);
CodeEdit::UndoScript script(*model, "Replace all", view->EditOwner(), view->GetSelection());
model->Replace(offset, length, text);
```

**Commands and menus.** Host windows route menu commands to their editor
with `CodeEdit::EditorCommands` (see `EditorCommands.h` for the three calls
a window needs). `EditorCommands::AddEditItems()` adds Undo/Redo (named after
the undo step), the clipboard and Select all; `AddShiftItems()`,
`AddFoldItems()` and `AddBookmarkItems()` shifting, folding and bookmarks;
`TextCommands::AddItems()` the whole-file text commands;
`FindKit::AddFindItems()` the Search menu. Each text command is a function
in `src/commands`, one per file.

**Settings.** `EditorSettings` sets tab width, indentation with spaces,
auto-indent, word wrap, read-only, margins, hard wrap and sticky scope
lines, per view.

**Syntax.** C, C++, shell (sh, Bash), Meson, Jam, Component Pascal, Python,
Make, CMake, Devicetree, HTML, CSS, JavaScript (with JSX), TypeScript and
TSX are built in; scripts and styles inside HTML are not highlighted yet.
Choose a language with
`TextModel::SetLanguage()`; `LanguageForPath()`, `LanguageForMimeType()` and
`LanguageForFile()` find one. Parsing runs incrementally in the background;
colors come from a `CodeEdit::Theme` (the default follows Pe).
`CodeEditView::Outline()` gives the definitions as a tree.

**Marks and decorations.** `TextModel::SharedMarks()` holds marks shown by
all views (breakpoints, bookmarks); a `MarkSet` or `DecorationSet` attached
to the model is shown by the views given it (`SetMarkSets()`,
`SetDecorationSets()`), e.g. the execution line of one debug session or the
coverage of one run. Marks follow edits.

**Hosts such as diff views** also use read-only views, host decorations
(`SetDecorations()`), host folds (`SetHostFolds()`), line geometry
(`LineTop()`, `LineAtY()`, `ScrollLineTo()`) and `SetModel()`.

The view sends notifications (contents changed, caret moved, gutter clicked,
viewport changed) to the target set with `SetTarget()`; see
`CodeEditView.h`.


## License

MIT, see [LICENSE](LICENSE).
