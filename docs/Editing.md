# Editing text

CodeEdit, GitTool and DirCompare share one text editor. This page describes
what works the same in all of them.

Keys are written as on a Haiku keyboard with default settings: **Cmd** is the
Alt key, **Opt** is the Windows key.


## Moving and selecting

| Keys | Action |
|---|---|
| Arrows | Move by character or line |
| Cmd + Left/Right | Move by word |
| Opt + Left/Right | Move by part of a word (camelCase, underscores) |
| Home / End | Start of the line (press again: first non-blank character) / end of the line |
| Cmd + Home/End | Start / end of the file |
| Page Up/Down | Move to the edge of the page, then by a page |
| Cmd + Up/Down | Previous / next function or type definition |
| Ctrl + arrows, Page Up/Down, Home/End | Scroll without moving the caret |
| Shift + any of the above | Extend the selection |
| Cmd + A | Select all |
| Cmd + B, Cmd + Shift + B | Grow / shrink the selection by syntax (the inside of brackets first) |

Mouse: a double click selects a word, or everything between a pair of
brackets when you click a bracket; a triple click selects a line. Dragging a
selection moves it; dragging with the middle button copies it, and with the
secondary button a menu asks whether to move or copy. Text can also be
dropped from other applications. Selecting past the edge of the view
scrolls it. The mouse wheel scrolls by three lines (Shift: by a page; Ctrl:
sideways).


## Typing and deleting

| Keys | Action |
|---|---|
| Tab | Insert a tab, or spaces when the file is set to indent with spaces |
| Enter | New line, indented like the current one |
| Cmd + [ / Cmd + ] | Remove / add one indent level on the selected lines |
| Ctrl + Backspace / Ctrl + Delete | Delete to the start / end of the line |
| Cmd + Z, Cmd + Shift + Z | Undo, redo |

Japanese, Chinese and other input methods work as usual. Wide characters take
two columns, so code stays aligned.


## Edit menu

**Undo** and **Redo** name what they will take back or do again, for example
"Undo Typing", "Undo Paste" or "Undo Replace all", and are disabled when there
is nothing to undo or redo. Undo is shared by all windows that show the same
file.

**Cut**, **Copy**, **Paste** and **Select all** work as in other Haiku
applications.

Below them are commands that change the whole file at once; each is one
step to undo:

- **Clean up whitespace**: removes spaces and tabs at the ends of lines and
  empty lines at the end of the file, and makes the file end with exactly
  one line break.
- **Convert tabs to spaces**: replaces each tab with the spaces it shows as.
- **Convert leading spaces to tabs**: rewrites the indentation at the start
  of each line with tabs, keeping its width.

The tab width comes from the file's options or its `.editorconfig` in
CodeEdit (see its [guide](../studio/README.md#file-options)); otherwise it
is 4.


## Search menu

| Item | Keys | Action |
|---|---|---|
| Find… | Cmd + F | Open the find panel |
| Find again | Cmd + G | Find the next match (Shift: backwards) |
| Find selection | Cmd + H | Search for the selected text (Shift: backwards) |
| Enter search string | Cmd + E | Put the selected text into the find panel |
| Replace | Cmd + = | Replace the selected match |
| Replace & find | Cmd + T | Replace the selected match and find the next one |
| Replace all | | Replace every match |
| Go to line… | Cmd + L | Ask for a line (and column) to go to |

The find panel is shared by all windows and searches the window you used
last. Its options: regular expressions (Perl-compatible, with `$1` etc. in
the replacement), ignore case, wrap around at the end, search backwards, and
replace all only within the selection.


## Folding and marks

Triangles in the gutter fold and unfold blocks of code; clicking the `…` after
a folded line unfolds it. Cmd + . folds the selected lines, or folds and
unfolds the block at the caret. Folds belong to each window.

Breakpoints, bookmarks and other line marks are shown in the gutter. In
CodeEdit, the View menu also has Fold all, Unfold all and the bookmark
commands, and the Edit menu has Shift left and Shift right.


## Syntax highlighting

Highlighting and the outline of definitions are available for C, C++, shell
scripts (sh, Bash), Meson, Jam, Component Pascal, Python, Make, CMake and
Devicetree sources. The language is chosen by the file type, then by the file
name; in CodeEdit it can also be set per file.
