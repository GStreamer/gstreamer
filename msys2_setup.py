#!/usr/bin/env python3
"""Setup meson based GStreamer uninstalled environment based on msys2."""

import argparse
import itertools
import os
import re
import sys
import shlex
import shutil
import subprocess
import tempfile

from common import git
from setup import GstBuildConfigurer


PROJECTNAME = "GStreamer build"

ROOTDIR = os.path.abspath(os.path.dirname(__file__))


class Msys2Configurer(GstBuildConfigurer):
    MESON_GIT = 'https://github.com/mesonbuild/meson.git'
    DEPENDENCIES = ['git',
                    'bison',
                    'mingw-w64-x86_64-pkg-config',
                    'mingw-w64-x86_64-ninja',
                    'mingw-w64-x86_64-libxml2',
                    'mingw-w64-x86_64-ffmpeg',
                    'mingw-w64-x86_64-python3',
                    'mingw-w64-x86_64-json-glib']
    LIBNAME_EXCEPTIONS = {
        r'^zlib1.lib$': 'z.lib',
        r'^nettle-.*': 'nettle.lib',
        r'^hogweed-.*': 'hogweed.lib',
        # Fancy, but it seems to be the correct way to do it
        r'^eay32.lib$': 'crypto.lib',
        r'^ssleay32.lib$': 'ssl.lib',
    }

    def get_libname(self, dll_name):
        lib_name = re.sub(r'(?:lib)?(.*?)(?:-\d+)?\.dll', r'\1.lib', dll_name)

        for exception_name, exception_libname in self.LIBNAME_EXCEPTIONS.items():
            if re.findall(exception_name, lib_name):
                return exception_libname
        return lib_name

    def make_lib(self, lib, dll, dll_name):
        print('%s... ' % os.path.basename(lib), end='', flush=True)
        try:
            os.remove(lib)
        except FileNotFoundError:
            pass

        dumpbin = subprocess.check_output(['dumpbin', '/exports', dll])
        lines = dumpbin.decode().splitlines()
        export_start = [i for i in enumerate(
            lines) if i[1].find('ordinal hint') != -1][0][0] + 2
        exports = itertools.takewhile(lambda x: x != '', lines[export_start:])
        exports = [i.split() for i in exports]
        def_file = tempfile.NamedTemporaryFile(
            suffix='.def', delete=False, mode='w')
        def_file.write('LIBRARY ' + dll_name + '\r\n')
        def_file.write('EXPORTS\r\n')
        for ordinal, _, _, name in exports:
            def_file.write(name + ' @' + ordinal + '\r\n')
        def_file.close()
        subprocess.check_output(['lib', '/def:' + def_file.name,
                                 '/out:' + lib])
        os.remove(def_file.name)

    def make_lib_if_needed(self, dll):
        if not dll.endswith('.dll'):
            return

        lib_dir, dll_name = os.path.split(dll)
        if lib_dir.endswith('bin'):
            lib_dir = lib_dir[:-3] + 'lib'

        lib_name = self.get_libname(dll_name)
        lib = os.path.join(lib_dir, lib_name)
        if os.path.exists(lib) and os.stat(dll).st_mtime_ns < os.stat(lib).st_mtime_ns:
            return

        print('Generating .lib file for %s ...' % os.path.basename(dll), end='', flush=True)
        self.make_lib(lib, dll, dll_name)
        print('DONE', flush=True)

    def make_libs(self):
        base = os.path.join(self.options.msys2_path, 'mingw64', 'bin')
        for f in os.listdir(base):
            if f.endswith('.dll'):
                self.make_lib_if_needed(os.path.join(base, f))

    def get_configs(self):
        return GstBuildConfigurer.get_configs(self) + [
            '-D' + m + ':disable_introspection=true' for m in [
                'gst-devtools', 'gstreamer', 'gst-plugins-base',
                'gst-editing-services']]

    def setup(self, args):
        if not os.path.exists(self.options.msys2_path):
            print("msys2 not found in %s. Please make sure to install"
                  " (from http://msys2.github.io/) specify --msys2-path"
                  " if you did not install in the default directory.", flush=True)
            return False

        for path in ['mingw64/bin', 'bin', 'usr/bin']:
            os.environ['PATH'] = os.environ.get(
                'PATH', '') + os.pathsep + os.path.normpath(os.path.join(self.options.msys2_path, path))
        os.environ['PATH'] = os.environ['PATH'].replace(';;', ';')
        os.environ['PKG_CONFIG_PATH'] = os.environ.get(
            'PKG_CONFIG_PATH', '') + ':/mingw64/lib/pkgconfig:/mingw64/share/pkgconfig'

        subprocess.check_call(['pacman', '-S', '--needed', '--noconfirm'] + self.DEPENDENCIES)
        source_path = os.path.abspath(os.path.curdir)

        print('Making sure meson is present in root folder... ', end='', flush=True)
        if not os.path.isdir(os.path.join(source_path, 'meson')):
            print('\nCloning meson', flush=True)
            git('clone', self.MESON_GIT, repository_path=source_path)
        else:
            print('\nDONE', flush=True)

        print("Making libs", flush=True)
        self.make_libs()
        print("Done making .lib files.", flush=True)
        if not os.path.exists(os.path.join(source_path, 'build', 'build.ninja')) or \
                self.options.reconfigure:
            print("Running meson", flush=True)
            if not self.configure_meson():
                return False

        try:
            if not args:
                print("Getting into msys2 environment", flush=True)
                subprocess.check_call([sys.executable,
                                os.path.join(source_path, 'gst-uninstalled.py'),
                                '--builddir', os.path.join(source_path, 'build')])
            else:
                print("Running %s" ' '.join(args), flush=True)
                res = subprocess.check_call(args)
        except subprocess.CalledProcessError as e:
            return False

        return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Process some integers.')
    parser.add_argument("--no-error", action='store_true',
                        default=False, help="Do not error out on warnings")
    parser.add_argument("--reconfigure", action='store_true',
                        default=False, help='Force a full reconfiguration'
                        ' meaning the build/ folder is removed.'
                        ' You can also use `ninja reconfigure` to just'
                        ' make sure meson is rerun but the build folder'
                        ' is kept.')
    if os.name != 'nt':
        print("Using this script outside windows does not make sense.", flush=True)
        exit(1)

    parser.add_argument("-m", "--msys2-path", dest="msys2_path",
                        help="Where to find msys2 root directory."
                        "(deactivates msys if unset)",
                        default="C:\msys64")

    parser.add_argument("-c", "--command", dest="command",
                        help="Command to run instead of entering environment.",
                        default="")
    options, args = parser.parse_known_args()

    if not shutil.which('cl'):
        print("Can not find MSVC on windows,"
                " make sure you are in a 'Visual Studio"
                " Native Tools Command Prompt'", flush=True)
        exit(1)

    configurer = Msys2Configurer(options, args)

    exit(not configurer.setup(shlex.split(options.command)))
