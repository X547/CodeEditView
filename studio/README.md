# CodeEdit

CodeEdit is a source code editor for Haiku that also runs a debugger (GDB) and
shows code coverage (LLVM). Text editing — keys, Edit and Search menus — is
described in [Editing text](../docs/Editing.md); this page covers the rest.


## Opening files

- Open files from Tracker, drop them on the CodeEdit icon, or run
  `CodeEdit file...` in Terminal. With CodeEdit as the preferred
  application for source files, `open file:42` (or `file:42:7`) in
  Terminal opens the file at that line, as do other applications that
  pass a line number.
- Started without files, CodeEdit shows a new, untitled document. Running it
  again without files — for example with a middle click on its Deskbar
  entry — opens another one.
- **File > Open…** (Cmd + O); the **Open** button of the tool bar also lists
  recently opened files.
- **File > Open from Git…** browses the files of any commit of a Git
  repository like a Tracker folder. Such files are read-only.
- **File > Open location…** (Cmd + Shift + O) takes a path or a location:
  `file:///boot/home/src/a.cpp`, or
  `git:///boot/home/src/repo?commit=HEAD&path=src/a.cpp` for a file of a
  commit.

The same file is never open twice: several windows can show it (**Window > New
view**, Cmd + Shift + N), and an edit in one appears in all of them at once.
Windows opened from a window join its window stack unless **Window > Open
windows in the same stack** is off. **Window > Documents** (Cmd + Shift + D)
lists all open files.


## Saving

**File > Save** (Cmd + S), **Save as…**, **Save copy as…** and **Revert**
work as usual. A `*` before the window title marks unsaved changes; closing
the last window of such a file asks whether to save.

If another program changes the file, CodeEdit loads the new version — unless
you have unsaved changes: then the status bar says so, and saving asks before
it overwrites the other program's version. Revert takes the version on disk.

Binary files and files with mixed line endings open read-only, so that
saving cannot change lines you did not edit. The lock button of the tool bar
(**Prevent editing**) makes a window read-only for a while.


## File options

The **info button** of the tool bar opens the options of the file:

- **Language** for highlighting and the outline: *Automatic* (from the file
  type, then the file name), plain text, or a language of your choice.
- **File type** (MIME type) of the file.
- **Indent with** tabs or spaces, **Tab width**, and **Indent width** (when
  indenting with spaces).
- **Line endings** (LF, CRLF or CR) and **UTF-8 byte order mark**, used when
  the file is saved. Changing them counts as an unsaved change.

OK applies them to all windows of the file. A file on disk keeps the options
in its attributes, so they are there the next time it is opened; for files
of Git commits and new documents they last until the file is closed.


## Finding your way

- **View > Outline** (Cmd + Shift + L) shows the functions, types and
  `#pragma mark` sections of the file beside it; the **Outline** button of
  the tool bar does the same. Click an entry to go there.
- **View > Sticky scopes**: the first lines of the functions and blocks you
  are in stay at the top of the window while you scroll.
- The **Definitions** button of the tool bar lists the functions and types;
  **Included files** lists the `#include`s and opens the chosen header.
- The **line:column field** beside the horizontal scroll bar: click it to go
  to a line. Press and hold it (or use the secondary button) for a menu of
  the folders containing the file, each with its contents: choose a folder to
  open it in Tracker, or a file to open it.


## Debugging

Debugging needs GDB 14 or later with Python (CodeEdit runs `gdb -i=dap`).

1. **Debug > Session > New session…** opens a session window: choose a
   program and its arguments to launch it, or a running program to attach to.
2. Set breakpoints by clicking the gutter or with **Toggle breakpoint**
   (Cmd + K). Shift + click or **Enable/disable breakpoint** (Cmd + Shift + K)
   turns one off without removing it. Breakpoints belong to the file and are
   used by all sessions.
3. Run and step from the Debug menu:

| Item | Keys |
|---|---|
| Continue | Cmd + R |
| Pause | Cmd + P |
| Step over | Cmd + N |
| Step into | Cmd + I |
| Step out | Cmd + Shift + I |
| Stop, Restart | |

When the program stops, the current line is marked in a window that follows
the session; one is opened if needed. The Debug menu also opens the
**Console** (GDB commands), **Threads**, **Stack**, **Variables** and
**Breakpoints** windows (Cmd + 1 … Cmd + 5).

Several sessions can run at once. Each window follows one session (or none),
chosen in **Debug > Session**, and shows only that session's lines.


## Code coverage

CodeEdit shows LLVM source-based coverage. Build the program with clang and
`-fprofile-instr-generate -fcoverage-mapping`, run it, and merge the profile
with `llvm-profdata merge`. `llvm-cov` must be installed.

**Coverage > Run > Open coverage…** (or `CodeEdit --coverage program
profile.profdata`) asks for the program and the profile. **Coverage files**
lists the files with how much of them was run; double-click one to open it.
A window following the run shows lines and parts of lines that never ran;
**Next uncovered line** (Cmd + J) and **Previous uncovered line**
(Cmd + Shift + J) jump between them. Like sessions, each window follows one
coverage run or none (**Coverage > Run**), independently of the session it
follows.
