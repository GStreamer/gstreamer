#!/usr/bin/env python3

import argparse
import os
import subprocess
from sys import exit


SCRIPTDIR = os.path.dirname(os.path.realpath(__file__))
# Look for the following build dirs: `build` `_build` `builddir`
DEFAULT_BUILDDIR = os.path.join(SCRIPTDIR, 'build')
if not os.path.exists(DEFAULT_BUILDDIR):
    DEFAULT_BUILDDIR = os.path.join(SCRIPTDIR, '_build')
if not os.path.exists(DEFAULT_BUILDDIR):
    DEFAULT_BUILDDIR = os.path.join(SCRIPTDIR, 'builddir')

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="gst-env")

    parser.add_argument("--builddir",
                        default=DEFAULT_BUILDDIR,
                        help="The meson build directory")
    parser.add_argument("--srcdir",
                        default=SCRIPTDIR,
                        help="The top level source directory")
    parser.add_argument("--sysroot",
                        default='',
                        help="The sysroot path used during cross-compilation")
    parser.add_argument("--wine",
                        default='',
                        help="Build a wine env based on specified wine command")
    parser.add_argument("--winepath",
                        default='',
                        help="Extra path to set to WINEPATH.")
    parser.add_argument("--only-environment",
                        action='store_true',
                        default=False,
                        help="Do not start a shell, only print required environment.")
    options, args = parser.parse_known_args()

    if not os.path.exists(options.builddir):
        print("GStreamer not built in %s\n\nBuild it and try again" %
              options.builddir)
        exit(1)
    options.builddir = os.path.abspath(options.builddir)

    cmd = ["meson", "devenv", "-C", options.builddir, "--workdir", os.getcwd()]
    if options.only_environment:
        cmd.append("--dump")
    else:
        cmd.extend(args)

    try:
        exit(subprocess.call(cmd, close_fds=False))
    except subprocess.CalledProcessError as e:
        exit(e.returncode)
