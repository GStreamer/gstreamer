#!/usr/bin/env python3

import argparse
import contextlib
import json
import os
import platform
import re
import site
import shutil
import subprocess
import sys
import tempfile
import pathlib

from distutils.sysconfig import get_python_lib

from common import get_meson
from common import git

SCRIPTDIR = os.path.dirname(os.path.realpath(__file__))
PREFIX_DIR = os.path.join(SCRIPTDIR, 'prefix')
# Use '_build' as the builddir instead of 'build'
DEFAULT_BUILDDIR = os.path.join(SCRIPTDIR, 'build')
if not os.path.exists(DEFAULT_BUILDDIR):
    DEFAULT_BUILDDIR = os.path.join(SCRIPTDIR, '_build')


def prepend_env_var(env, var, value):
    env[var] = os.pathsep + value + os.pathsep + env.get(var, "")
    env[var] = env[var].replace(os.pathsep + os.pathsep, os.pathsep).strip(os.pathsep)


def get_subprocess_env(options, gst_version):
    env = os.environ.copy()

    env["CURRENT_GST"] = os.path.normpath(SCRIPTDIR)
    env["GST_VALIDATE_SCENARIOS_PATH"] = os.path.normpath(
        "%s/subprojects/gst-devtools/validate/data/scenarios" % SCRIPTDIR)
    env["GST_VALIDATE_PLUGIN_PATH"] = os.path.normpath(
        "%s/subprojects/gst-devtools/validate/plugins" % options.builddir)
    env["GST_VALIDATE_APPS_DIR"] = os.path.normpath(
        "%s/subprojects/gst-editing-services/tests/validate" % SCRIPTDIR)
    prepend_env_var(env, "PATH", os.path.normpath(
        "%s/subprojects/gst-devtools/validate/tools" % options.builddir))
    prepend_env_var(env, "PATH", os.path.join(SCRIPTDIR, 'meson'))
    env["GST_VERSION"] = gst_version
    env["GST_ENV"] = 'gst-' + gst_version
    env["GST_PLUGIN_SYSTEM_PATH"] = ""
    env["GST_PLUGIN_SCANNER"] = os.path.normpath(
        "%s/subprojects/gstreamer/libs/gst/helpers/gst-plugin-scanner" % options.builddir)
    env["GST_PTP_HELPER"] = os.path.normpath(
        "%s/subprojects/gstreamer/libs/gst/helpers/gst-ptp-helper" % options.builddir)
    env["GST_REGISTRY"] = os.path.normpath(options.builddir + "/registry.dat")

    sharedlib_reg = re.compile(r'\.so|\.dylib|\.dll')
    typelib_reg = re.compile(r'.*\.typelib$')
    pluginpath_reg = re.compile(r'lib.*' + re.escape(os.path.normpath('/gstreamer-1.0/')))

    if os.name is 'nt':
        lib_path_envvar = 'PATH'
    elif platform.system() == 'Darwin':
        lib_path_envvar = 'DYLD_LIBRARY_PATH'
    else:
        lib_path_envvar = 'LD_LIBRARY_PATH'

    prepend_env_var(env, "GST_PLUGIN_PATH", os.path.join(SCRIPTDIR, 'subprojects',
                                                        'gst-python', 'plugin'))
    prepend_env_var(env, "GST_PLUGIN_PATH", os.path.join(PREFIX_DIR, 'lib',
                                                        'gstreamer-1.0'))
    prepend_env_var(env, "PATH", os.path.join(PREFIX_DIR, 'bin'))
    prepend_env_var(env, lib_path_envvar, os.path.join(PREFIX_DIR, 'lib'))
    prepend_env_var(env, "GST_VALIDATE_SCENARIOS_PATH", os.path.join(
        PREFIX_DIR, 'share', 'gstreamer-1.0', 'validate', 'scenarios'))
    prepend_env_var(env, "GI_TYPELIB_PATH", os.path.join(PREFIX_DIR, 'lib',
                                                         'lib', 'girepository-1.0'))
    prepend_env_var(env, "PKG_CONFIG_PATH", os.path.join(PREFIX_DIR, 'lib', 'pkgconfig'))

    meson = get_meson()
    targets_s = subprocess.check_output(meson + ['introspect', options.builddir, '--targets'])
    targets = json.loads(targets_s.decode())
    paths = set()
    mono_paths = set()
    for target in targets:
        filename = target['filename']
        root = os.path.dirname(filename)
        if filename.endswith('.dll'):
            mono_paths.add(os.path.join(options.builddir, root))
        if typelib_reg.search(filename):
            prepend_env_var(env, "GI_TYPELIB_PATH",
                            os.path.join(options.builddir, root))
        elif sharedlib_reg.search(filename):
            if target.get('type') != "shared library":
                continue

            if target.get('installed') and pluginpath_reg.search(os.path.normpath(target.get('install_filename'))):
                prepend_env_var(env, "GST_PLUGIN_PATH", os.path.join(options.builddir, root))
                continue

            prepend_env_var(env, lib_path_envvar,
                            os.path.join(options.builddir, root))
        elif target.get('type') == 'executable' and target.get('installed'):
            paths.add(os.path.join(options.builddir, root))

    for p in paths:
        prepend_env_var(env, 'PATH', p)

    if os.name != 'nt':
        for p in mono_paths:
            prepend_env_var(env, "MONO_PATH", p)

    presets = set()
    encoding_targets = set()
    pkg_dirs = set()
    python_dirs = set(["%s/subprojects/gstreamer/libs/gst/helpers/" % options.srcdir])
    if '--installed' in subprocess.check_output(meson + ['introspect', '-h']).decode():
        installed_s = subprocess.check_output(meson + ['introspect', options.builddir, '--installed'])
        for path, installpath in json.loads(installed_s.decode()).items():
            installpath_parts = pathlib.Path(installpath).parts
            path_parts = pathlib.Path(path).parts

            # We want to add all python modules to the PYTHONPATH
            # in a manner consistent with the way they would be imported:
            # For example if the source path /home/meh/foo/bar.py
            # is to be installed in /usr/lib/python/site-packages/foo/bar.py,
            # we want to add /home/meh to the PYTHONPATH.
            # This will only work for projects where the paths to be installed
            # mirror the installed directory layout, for example if the path
            # is /home/meh/baz/bar.py and the install path is
            # /usr/lib/site-packages/foo/bar.py , we will not add anything
            # to PYTHONPATH, but the current approach works with pygobject
            # and gst-python at least.
            if 'site-packages' in installpath_parts:
                install_subpath = os.path.join(*installpath_parts[installpath_parts.index('site-packages') + 1:])
                if path.endswith(install_subpath):
                    python_dirs.add(path[:len (install_subpath) * -1])

            if path.endswith('.prs'):
                presets.add(os.path.dirname(path))
            elif path.endswith('.gep'):
                encoding_targets.add(
                    os.path.abspath(os.path.join(os.path.dirname(path), '..')))
            elif path.endswith('.pc'):
                # Is there a -uninstalled pc file for this file?
                uninstalled = "{0}-uninstalled.pc".format(path[:-3])
                if os.path.exists(uninstalled):
                    pkg_dirs.add(os.path.dirname(path))

        for p in presets:
            prepend_env_var(env, 'GST_PRESET_PATH', p)

        for t in encoding_targets:
            prepend_env_var(env, 'GST_ENCODING_TARGET_PATH', t)

        for pkg_dir in pkg_dirs:
            prepend_env_var(env, "PKG_CONFIG_PATH", pkg_dir)
    prepend_env_var(env, "PKG_CONFIG_PATH", os.path.join(options.builddir,
                                                         'subprojects',
                                                         'gst-plugins-good',
                                                         'pkgconfig'))

    for python_dir in python_dirs:
        prepend_env_var(env, 'PYTHONPATH', python_dir)

    mesonpath = os.path.join(SCRIPTDIR, "meson")
    if os.path.join(mesonpath):
        # Add meson/ into PYTHONPATH if we are using a local meson
        prepend_env_var(env, 'PYTHONPATH', mesonpath)

    return env

# https://stackoverflow.com/questions/1871549/determine-if-python-is-running-inside-virtualenv
def in_venv():
    return (hasattr(sys, 'real_prefix') or
            (hasattr(sys, 'base_prefix') and sys.base_prefix != sys.prefix))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(prog="gstreamer-uninstalled")

    parser.add_argument("--builddir",
                        default=DEFAULT_BUILDDIR,
                        help="The meson build directory")
    parser.add_argument("--srcdir",
                        default=SCRIPTDIR,
                        help="The top level source directory")
    options, args = parser.parse_known_args()

    if not os.path.exists(options.builddir):
        print("GStreamer not built in %s\n\nBuild it and try again" %
              options.builddir)
        exit(1)
    options.builddir = os.path.abspath(options.builddir)

    if not os.path.exists(options.srcdir):
        print("The specified source dir does not exist" %
              options.srcdir)
        exit(1)

    # The following incantation will retrieve the current branch name.
    gst_version = git("rev-parse", "--symbolic-full-name", "--abbrev-ref", "HEAD",
                      repository_path=options.srcdir).strip('\n')

    if not args:
        if os.name is 'nt':
            args = [os.environ.get("COMSPEC", r"C:\WINDOWS\system32\cmd.exe")]
        else:
            args = [os.environ.get("SHELL", os.path.realpath("/bin/sh"))]
        if "bash" in args[0]:
            bashrc = os.path.expanduser('~/.bashrc')
            if os.path.exists(bashrc):
                tmprc = tempfile.NamedTemporaryFile(mode='w')
                with open(bashrc, 'r') as src:
                    shutil.copyfileobj(src, tmprc)
                tmprc.write('\nexport PS1="[gst-%s] $PS1"' % gst_version)
                tmprc.flush()
                # Let the GC remove the tmp file
                args.append("--rcfile")
                args.append(tmprc.name)
    try:
        exit(subprocess.call(args, cwd=options.srcdir, close_fds=False,
                             env=get_subprocess_env(options, gst_version)))
    except subprocess.CalledProcessError as e:
        exit(e.returncode)
