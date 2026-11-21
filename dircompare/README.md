# DirCompare

DirCompare compares two folders, or two files, and lets you merge them: copy
files and changes from one side to the other and edit both sides. Text
editing is described in [Editing text](../docs/Editing.md).


## Starting a comparison

Select two folders or two files in Tracker and open them with DirCompare,
drop them on the DirCompare icon, or run `CodeEditDirCompare left right` in
Terminal. Otherwise — and with **File > New comparison…** (Cmd + N) — a
window asks for the two paths; they can be dropped there from Tracker too.

Folders are read in the background, then files of the same size are compared
byte by byte; a progress window shows how far it got. Cancelling it keeps the
comparison shown before. **File > Refresh** (Cmd + Shift + R) compares again.


## The folder tree

The tree lists what differs: files with different contents, files on only
one side, and entries that are a file on one side and a folder on the other.
**View > Show identical files** lists everything. Version control folders
(`.git` and the like) are skipped unless **View > Ignore version control
directories** is turned off.

The **Files** menu copies the selected file or folder to the other side
(keeping permissions, times and attributes; folders are merged), deletes it
from one side, or shows it in Tracker. Copying and deleting ask first;
deleting cannot be undone. **View > Swap sides** exchanges left and right.


## Comparing files

Selecting a file shows both versions side by side. Shapes between the panes
connect the changed parts; their arrows copy a change to the other side.
Both sides can be edited.

| Item | Keys |
|---|---|
| Copy change to left / right (at the caret or the selected lines) | Cmd + Opt + Left / Right |
| Next / previous change | Cmd + Shift + Down / Up |
| Next / previous different file | Cmd + Opt + Down / Up |
| Save the shown file | Cmd + S |
| Save all | Cmd + Shift + S |

Saving a file that exists on one side only creates it on the other.
**File > Reload from disk** reads the shown file again, asking first if you
edited it. Closing the window or swapping the sides with unsaved files asks
whether to save them.

The **View** menu turns synchronized scrolling, hiding unchanged lines and
word wrap on or off, and chooses the diff algorithm (Myers, Minimal or
Patience).
