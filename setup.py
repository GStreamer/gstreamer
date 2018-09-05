#!/usr/bin/env python3
"""Script for generating the Makefiles."""

import argparse
import os
import sys
import shutil
import subprocess

from common import get_meson
from common import accept_command


PROJECTNAME = "GStreamer build"

ROOTDIR = os.path.abspath(os.path.dirname(__file__))


class GstBuildConfigurer:

    def __init__(self, options, args):
        self.options = options
        self.args = args

    def get_configs(self):
        if self.options.no_error:
            return []
        return ['--werror']

    def configure_meson(self):
        if not self.options.reconfigure:
            if os.path.exists(ROOTDIR + "/build/build.ninja"):
                print("Not reconfiguring")
                return True

        meson = get_meson()
        if not meson:
            print("Install mesonbuild to build %s: http://mesonbuild.com/\n"
                  "You can simply install it with:\n"
                  "    $ sudo pip3 install meson" % PROJECTNAME)
            return False

        ninja = accept_command(["ninja", "ninja-build"])
        if not ninja:
            print("Install ninja-build to build %s: https://ninja-build.org/"
                  % PROJECTNAME)
            return False

        build_dir = os.path.join(ROOTDIR, "build")
        shutil.rmtree(build_dir, True)
        os.mkdir(build_dir)

        try:
            subprocess.check_call(
                [sys.executable, meson, "../"] + self.args + self.get_configs(), cwd=build_dir)
            print("\nYou can now build GStreamer and its various subprojects running:\n"
                  " $ {} -C {!r}".format(os.path.basename(ninja), build_dir))
        except subprocess.CalledProcessError:
            return False

        return True

    def setup(self):
        return self.configure_meson()



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Process some integers.')
    parser.add_argument("--reconfigure", action='store_true',
                        default=False, help='Force a full reconfiguration'
                        ' meaning the build/ folder is removed.'
                        ' You can also use `ninja reconfigure` to just'
                        ' make sure meson is rerun but the build folder'
                        ' is kept.')
    parser.add_argument("--no-error", action='store_true',
                        default=True, help="Do not error out on warnings")

    options, args = parser.parse_known_args()
    configurer = GstBuildConfigurer(options, args)
    exit(not configurer.setup())
