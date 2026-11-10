#!/usr/bin/env python3
# Fails if a dialog is waited for (BAlert::Go(), ModalDialog::Go(),
# Form::Go()) outside the code that runs on threads that are not loopers'.
# Window threads must use DiffKit::Ask() and the Open() methods instead,
# or the window freezes while the dialog is open.

import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1])
folders = ["gittool", "gitdiff", "diffkit", "findkit", "dircompare", "studio", "coverage", "debugger", "src"]
# Network work runs on its own thread.
allowed = {"gittool/NetworkTask.cpp", "gittool/Dialogs.cpp"}
# Go() without arguments, or Form::Go(...): both wait. Go(invoker) and
# Go(nullptr) on an alert do not.
blocking = re.compile(r"(->|\.)Go\(\)|\bform\.Go\(")

problems = []
for folder in folders:
    for path in sorted((root / folder).rglob("*.cpp")):
        relative = path.relative_to(root).as_posix()
        if relative in allowed:
            continue
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if blocking.search(line) and not line.lstrip().startswith("//"):
                problems.append(f"{relative}:{number}: {line.strip()}")

for problem in problems:
    print(problem)
if problems:
    print("Waiting for a dialog blocks the calling thread; on a window thread, use DiffKit::Ask() or Open().")
sys.exit(1 if problems else 0)
