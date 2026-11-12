#!/usr/bin/env python3
# Meson build step for Haiku applications: adds compiled resources to a
# linked executable in place, then writes a stamp file for the build.
#
# Arguments: xres executable resources stamp.

import subprocess
import sys

xres, executable, resources, stamp = sys.argv[1:5]
subprocess.run([xres, "-o", executable, resources], check=True)
with open(stamp, "w", encoding="utf-8", newline="\n"):
	pass
