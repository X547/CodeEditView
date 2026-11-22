# GitTool

GitTool manages Git repositories on Haiku: worktrees, branches, remotes and
tags, staging and committing, and rewriting history. It works on the
repository directly (with libgit2); no `git` command is needed.

Every operation that changes something is shown first: what will change,
which files are affected, and what would be lost. Nothing happens until you
choose **Run**, and if commits or changes would be lost, you must confirm that
too.

Text editing in GitTool's diff windows is described in
[Editing text](../docs/Editing.md).


## Opening a repository

Open a repository folder (or any folder inside it) from Tracker, drop it on
the GitTool icon, run `CodeEditGitTool folder` in Terminal, or use
**Repository > Open repository…** (Cmd + O). Each repository gets one window;
opening it again shows that window.

The window lists **Worktrees**, **Branches**, **Remotes** and **Tags** on the
left. The buttons above the list act on the category (for example **Create
branch**); the **Actions** menu and the context menu of an entry (secondary
click) act on the selected entry. Double-clicking a worktree opens its diff
window, double-clicking a branch or tag shows its history. Below the list are
details of the selection and a log of what was done. The window follows
changes made by other programs.


## Worktrees

A worktree is a folder with checked-out files. Its state column counts its
conflicts, staged, changed and untracked files. Counting needs a look at every
file, so in a large repository the counts come a moment after the lists and
show "checking…" until then. Besides creating and removing worktrees, you can:

- **Open in Git Diff**: stage, edit and commit its changes (see below).
- **Open folder** in Tracker.
- **Set branch**, **Set commit (no branch)**, **Create branch here**.
- **Unstage all**, **Discard changes** (optionally untracked files too).
- **Abort unfinished operation** (a merge or rebase left by another program).
- **Protect** a worktree so that it is not removed, with a reason.

When moving a worktree to another commit would touch files you changed, the
preview offers to cancel, to move only the position and keep your files, or
to discard the overlapping changes.


## Branches

- **Create branch**, **Rename**, **Delete**, **Set commit**.
- **Update from tracked**: bring in the downloaded changes of the remote
  branch it tracks — fast-forward, merge or replay (rebase). **Update all
  from tracked** fast-forwards every branch that is only behind.
- **Merge another branch into this**, **Replay onto** (rebase), **Replay
  interactively onto** (in the history editor).
- **Upload** (push) to a remote, also overwriting the server's version if
  you choose; **Set tracked remote branch**, **Stop tracking**.
- **Upload for review (Gerrit)** with topic, work-in-progress or ready,
  reviewers and CC.
- **Set in worktree**, **Set in new worktree**, **Add tag at this branch's
  commit**.
- **Previous positions** lists where the branch was before (its reflog) and
  can move it back; **Undo last change** does so for the last change.
- **Clean up** offers to delete branches that are merged or whose remote
  branch was deleted.
- **Show history**, **Edit history** (see below).


## Remotes and tags

Remotes: **Add remote**, **Edit URLs**, **Rename**, **Remove**, **Download**
(fetch; **Repository > Download from all**, Cmd + D, downloads from every
remote), **Check what's on the server** (compare without downloading), and
**Download review change (Gerrit)** by change number or link. Downloaded
remote branches can become local branches, be tracked, be set in a worktree
or be deleted on the server.

Tags: **Create tag** (with a message, or a simple one), **Move to commit**,
**Edit message**, **Rename**, **Delete locally**, **Delete on server**,
**Upload**, **Upload tags**, **Download tags**, **Create branch from tag**. A
filter field narrows long tag lists.


### Connecting to servers

For SSH remotes, GitTool uses the SSH agent if one runs, then the keys
`id_ed25519`, `id_ecdsa` or `id_rsa` in `~/config/settings/ssh`. It follows
the OpenSSH client configuration there and in `/boot/system/settings/ssh`:
host aliases, host names, users, ports and identity files
(`ProxyCommand` and `ProxyJump` are not supported). It asks before
connecting to a server it does not know, showing its key fingerprint. For HTTPS remotes it asks for a user name and
password. Long downloads and uploads show their progress and can be
cancelled.


## Staging and committing

**Open in Git Diff** on a worktree opens its diff window. It lists the files
with unstaged and staged changes; selecting one shows two panes:

- an unstaged file: the staged version (left) and the working file (right)
- a staged file: the last commit (left) and the staged version (right)

The right pane can be edited; **File > Save** (Cmd + S) writes it (the
staged version of a staged file), **Save all** (Cmd + Shift + S) writes every
edited file.

| Changes menu | Keys |
|---|---|
| Stage/unstage chunk or selected lines | Cmd + U |
| Revert chunk or selected lines | Cmd + R |
| Stage/unstage file (also a double click in the list) | Cmd + Shift + U |
| Commit staged changes | Cmd + K |
| Next / previous change | Cmd + Shift + Down / Up |

The arrows between the panes stage or unstage a single change; staging takes
effect at once. Reverting changes the working file in the window; save it to
keep the result.

Below the list is the commit message. **Amend last commit** replaces the
last commit and starts from its message; **Add Change-Id** adds the line
Gerrit needs. The message editor follows Git's convention: it wraps the body
at 72 columns and highlights a subject longer than 50 characters, text on
the second line and body lines that are too long.

Click the title of a pane to open that version in CodeEdit.

The **View** menu turns synchronized scrolling, hiding unchanged lines and
word wrap on or off, and chooses the diff algorithm.

Binary files, files with mixed line endings, conflicts, symbolic links,
submodules and files with filter attributes are shown read-only.


## History

**Show history** lists the commits of a branch, remote branch, tag or commit.
**Changes…** shows what a commit changed, compared with any of its parents,
and can open both versions of a file in CodeEdit.

### Editing history

Turn on **Edit** in the history window of a local branch (or use **Edit
history**) to rewrite its commits back to the last merge:

- reorder commits by dragging them; drop commits from another history window
  to copy them in
- **Remove**, **Combine** with the older commit before it (**Separate**
  undoes that), **Reword…** the message
- **Edit…** the files of a commit, or **Split…** it in two
- **Resolve…** conflicts that the new order causes, file by file
- **Rebase…** onto another commit

Nothing changes until **Apply…**, which moves the branch once after a
preview; **Reset** drops the pending edits. The edit and split windows show
the commit's files as a diff: **Take change or selected lines from the left**
(Cmd + U), **Use the left version of the file**, **Delete the file**, or edit
the right side directly.
