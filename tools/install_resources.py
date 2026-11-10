#!/usr/bin/env python3
# Meson install script for Haiku applications: adds compiled resources
# (signature, flags, file types) to an installed executable and updates its
# MIME information. Installing into the live system (no DESTDIR) also enters
# the application into the MIME database, so its file types take effect.
#
# Arguments: xres mimeset resources installed-path (relative to the prefix,
# or absolute).

import os
import subprocess
import sys

xres, mimeset, resources, installed = sys.argv[1:5]
destdir = os.environ.get("DESTDIR", "")
if os.path.isabs(installed):
    path = destdir + installed if destdir else installed
else:
    path = os.path.join(os.environ["MESON_INSTALL_DESTDIR_PREFIX"], installed)

print(f"Adding resources to {path}")
subprocess.run([xres, "-o", path, resources], check=True)
if destdir:
    subprocess.run([mimeset, "-f", path], check=True)
else:
    subprocess.run([mimeset, "--all", "-f", path], check=True)
