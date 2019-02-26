#!/usr/bin/env python3

import subprocess
import os
import sys
import shutil

def accept_command(commands):
    """Search @commands and returns the first found absolute path."""
    for command in commands:
        command = shutil.which(command)
        if command:
            return command

    return None

if __name__ == "__main__":
    ninja = accept_command(["ninja", "ninja-build"])
    buildroot = os.environ["MESON_BUILD_ROOT"]

    bindinate  = False
    if len(sys.argv) > 1 and sys.argv[1] == "bindinate":
        bindinate  = True

    print("Building all code")
    subprocess.check_call([ninja, "-C", buildroot])

    if 'gstreamer-sharp' in os.environ['MESON_SUBDIR']:
        subproject_prefix = 'gstreamer-sharp@@'
    else:
        subproject_prefix = ''

    if bindinate:
        print("Bindinate GStreamer")
        subprocess.check_call([ninja, "-C", buildroot, subproject_prefix + "bindinate_gstreamer"])

    print("Update GStreamer bindings")
    subprocess.check_call([ninja, "-C", buildroot, subproject_prefix + "update_gstreamer_code"])

    if bindinate:
        print("Bindinate GES")
        subprocess.check_call([ninja, "-C", buildroot, subproject_prefix + "bindinate_ges"])
    print("Update GES bindings")
    subprocess.check_call([ninja, "-C", buildroot, subproject_prefix + "update_ges_code"])

    print("Building all code")
    subprocess.check_call([ninja, "-C", buildroot])