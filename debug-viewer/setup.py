#!/usr/bin/env python
# -*- coding: utf-8; mode: python; -*-
#
#  GStreamer Debug Viewer - View and analyze GStreamer debug log files
#
#  Copyright (C) 2007 René Stadler <mail@renestadler.de>
#
#  This program is free software; you can redistribute it and/or modify it
#  under the terms of the GNU General Public License as published by the Free
#  Software Foundation; either version 3 of the License, or (at your option)
#  any later version.
#
#  This program is distributed in the hope that it will be useful, but WITHOUT
#  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
#  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
#  more details.
#
#  You should have received a copy of the GNU General Public License along with
#  this program.  If not, see <http://www.gnu.org/licenses/>.

"""GStreamer Debug Viewer distutils setup script."""

import sys
import os
import os.path

import distutils.cmd
from setuptools import setup
from distutils.command.clean import clean
from distutils.command.build import build
from distutils.command.sdist import sdist
from distutils.command.install_scripts import install_scripts
from distutils.errors import *


def perform_substitution(filename, values):

    fp = file(filename, "rt")
    data = fp.read()
    fp.close()

    for name, value in list(values.items()):
        data = data.replace("$%s$" % (name,), value)

    fp = file(filename, "wt")
    fp.write(data)
    fp.close()


class clean_custom (clean):

    def remove_file(self, path):

        if os.path.exists(path):
            print("removing '%s'" % (path,))
            if not self.dry_run:
                os.unlink(path)

    def remove_directory(self, path):

        from distutils import dir_util

        if os.path.exists(path):
            dir_util.remove_tree(path, dry_run=self.dry_run)

    def run(self):

        clean.run(self)

        if os.path.exists("MANIFEST.in"):
            # MANIFEST is generated, get rid of it.
            self.remove_file("MANIFEST")

        pot_file = os.path.join("po", "gst-debug-viewer.pot")
        self.remove_file(pot_file)

        self.remove_directory("build")
        self.remove_directory("dist")

        for path, dirs, files in os.walk("."):
            for filename in files:
                if filename.endswith(".pyc") or filename.endswith(".pyo"):
                    file_path = os.path.join(path, filename)
                    self.remove_file(file_path)


class build_custom (build):

    def build_l10n(self):

        return self.l10n

    sub_commands = build.sub_commands + [("build_l10n", build_l10n,)]
    user_options = build.user_options + \
        [("l10n", None, "enable translations",)]
    boolean_options = build.boolean_options + ["l10n"]

    def initialize_options(self):

        build.initialize_options(self)

        self.l10n = False


class build_l10n (distutils.cmd.Command):

    # Based on code from python-distutils-extra by Sebastian Heinlein.

    description = "gettext framework integration"

    user_options = [("merge-desktop-files=", "m", ".desktop.in files to merge"),
                    ("merge-xml-files=", "x", ".xml.in files to merge"),
                    ("merge-schemas-files=", "s", ".schemas.in files to merge"),
                    ("merge-rfc822deb-files=", "d", "RFC822 files to merge"),
                    ("merge-key-files=", "k", ".key.in files to merge"),
                    ("domain=", "d", "gettext domain"),
                    ("bug-contact=", "c", "contact address for msgid bugs")]

    def initialize_options(self):

        self.merge_desktop_files = []
        self.merge_xml_files = []
        self.merge_key_files = []
        self.merge_schemas_files = []
        self.merge_rfc822deb_files = []
        self.domain = None
        self.bug_contact = None

    def finalize_options(self):

        for attr in ("desktop", "xml", "key", "schemas", "rfc822deb",):
            value = getattr(self, "merge_%s_files" % (attr,))
            if not value:
                value = []
            else:
                value = eval(value)
            setattr(self, "merge_%s_files" % (attr,), value)

        if self.domain is None:
            self.domain = self.distribution.metadata.name

    def run(self):

        from glob import glob

        data_files = self.distribution.data_files

        po_makefile = os.path.join("po", "Makefile")
        if os.path.exists(po_makefile):
            raise DistutilsFileError("file %s exists (intltool will pick up "
                                     "values from there)" % (po_makefile,))

        cwd = os.getcwd()

        if self.bug_contact is not None:
            os.environ["XGETTEXT_ARGS"] = "--msgid-bugs-address=%s" % (
                self.bug_contact,)
        os.chdir(os.path.join(cwd, "po"))
        # Update .pot file.
        self.spawn(["intltool-update", "-p", "-g", self.domain])
        # Merge new strings into .po files.
        self.spawn(["intltool-update", "-r", "-g", self.domain])

        os.chdir(cwd)

        for po_file in glob(os.path.join("po", "*.po")):
            lang = os.path.basename(po_file[:-3])
            if lang.startswith("."):
                # Hidden file, like auto-save data from an editor.
                continue
            mo_dir = os.path.join("build", "mo", lang, "LC_MESSAGES")
            mo_file = os.path.join(mo_dir, "%s.mo" % (self.domain,))
            self.mkpath(mo_dir)
            self.spawn(["msgfmt", po_file, "-o", mo_file])

            targetpath = os.path.join("share", "locale", lang, "LC_MESSAGES")
            data_files.append((targetpath, (mo_file,)))

        for parameter, option in ((self.merge_xml_files, "-x",),
                                  (self.merge_desktop_files, "-d",),
                                  (self.merge_schemas_files, "-s",),
                                  (self.merge_rfc822deb_files, "-r",),
                                  (self.merge_key_files, "-k",),):
            if not parameter:
                continue
            for target, files in parameter:
                build_target = os.path.join("build", target)
                for file in files:
                    if file.endswith(".in"):
                        file_merged = os.path.basename(file[:-3])
                    else:
                        file_merged = os.path.basename(file)

                self.mkpath(build_target)
                file_merged = os.path.join(build_target, file_merged)
                self.spawn(["intltool-merge", option, "po", file, file_merged])
                data_files.append((target, [file_merged],))


class distcheck (sdist):

    # Originally based on code from telepathy-python.

    description = "verify self-containedness of source distribution"

    def run(self):

        from distutils import dir_util
        from distutils.spawn import spawn

        # This creates e.g. dist/gst-debug-viewer-0.1.tar.gz.
        sdist.run(self)

        base_dir = self.distribution.get_fullname()
        distcheck_dir = os.path.join(self.dist_dir, "distcheck")
        self.mkpath(distcheck_dir)
        self.mkpath(os.path.join(distcheck_dir, "again"))

        cwd = os.getcwd()
        os.chdir(distcheck_dir)

        if os.path.isdir(base_dir):
            dir_util.remove_tree(base_dir)

        # Unpack tarball into dist/distcheck, creating
        # e.g. dist/distcheck/gst-debug-viewer-0.1.
        for archive in self.archive_files:
            if archive.endswith(".tar.gz"):
                archive_rel = os.path.join(os.pardir, os.pardir, archive)
                spawn(["tar", "-xzf", archive_rel, base_dir])
                break
        else:
            raise ValueError("no supported archives were created")

        os.chdir(cwd)
        os.chdir(os.path.join(distcheck_dir, base_dir))
        spawn([sys.executable, "setup.py", "sdist", "--formats", "gztar"])

        # Unpack tarball into dist/distcheck/again.
        os.chdir(cwd)
        os.chdir(os.path.join(distcheck_dir, "again"))
        archive_rel = os.path.join(
            os.pardir, base_dir, "dist", "%s.tar.gz" % (base_dir,))
        spawn(["tar", "-xzf", archive_rel, base_dir])

        os.chdir(cwd)
        os.chdir(os.path.join(distcheck_dir, base_dir))
        spawn([sys.executable, "setup.py", "clean"])

        os.chdir(cwd)
        spawn(["diff", "-ru",
               os.path.join(distcheck_dir, base_dir),
               os.path.join(distcheck_dir, "again", base_dir)])

        if not self.keep_temp:
            dir_util.remove_tree(distcheck_dir)


class install_scripts_custom (install_scripts):

    user_options = install_scripts.user_options \
        + [("substitute-files=", None,
            "files to perform substitution on")]

    def initialize_options(self):

        install_scripts.initialize_options(self)

        self.substitute_files = "[]"

    def run(self):

        from os.path import normpath

        install = self.distribution.get_command_obj("install")
        install.ensure_finalized()

        values = {"DATADIR": install.install_data or "",
                  "PREFIX": install.home or install.prefix or "",
                  "SCRIPTSDIR": self.install_dir or ""}

        if install.home:
            values["LIBDIR"] = os.path.normpath(install.install_lib)

        if install.root:
            root = normpath(install.root)
            len_root = len(root)
            for name, value in list(values.items()):
                if normpath(value).startswith(root):
                    values[name] = normpath(value)[len_root:]

        # Perform installation as normal...
        install_scripts.run(self)

        if self.dry_run:
            return

        # ...then substitute in-place:
        for filename in eval(self.substitute_files):
            perform_substitution(os.path.join(
                self.install_dir, filename), values)


cmdclass = {"build": build_custom,
            "clean": clean_custom,
            "install_scripts": install_scripts_custom,

            "build_l10n": build_l10n,
            "distcheck": distcheck}

setup(cmdclass=cmdclass,

      packages=["GstDebugViewer",
                "GstDebugViewer.Common",
                "GstDebugViewer.GUI",
                "GstDebugViewer.Plugins",
                "GstDebugViewer.tests"],
      scripts=["gst-debug-viewer"],
      data_files=[("share/gst-debug-viewer", ["data/about-dialog.ui",
                                              "data/main-window.ui",
                                              "data/menus.ui"],),
                  ("share/icons/hicolor/48x48/apps",
                   ["data/gst-debug-viewer.png"],),
                  ("share/icons/hicolor/scalable/apps", ["data/gst-debug-viewer.svg"],)],

      name="gst-debug-viewer",
      version="0.1",
      description="GStreamer Debug Viewer",
      long_description="""""",
      test_suite="GstDebugViewer.tests",
      license="GNU GPL",
      author="Rene Stadler",
      author_email="mail@renestadler.de",
      url="http://renestadler.de/projects/gst-debug-viewer")
