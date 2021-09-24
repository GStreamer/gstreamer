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

"""GStreamer Debug Viewer Main module."""

import sys
import optparse
from gettext import gettext as _, ngettext

from gi.repository import GLib

from GstDebugViewer import GUI
import GstDebugViewer.Common.Main
Common = GstDebugViewer.Common

GETTEXT_DOMAIN = "gst-debug-viewer"


def main_version(opt, value, parser, *args, **kwargs):

    from GstDebugViewer import version

    print("GStreamer Debug Viewer %s" % (version,))
    sys.exit(0)


class Paths (Common.Main.PathsProgramBase):

    program_name = "gst-debug-viewer"


def main():
    parser = optparse.OptionParser(
        _("%prog [OPTION...] [FILENAME]"),
        description=_("Display and analyze GStreamer debug log files"))
    parser.add_option("--version", "-v",
                      action="callback",
                      dest="version",
                      callback=main_version,
                      help=_("Display version and exit"))

    Common.Main.main(main_function=GUI.main,
                     option_parser=parser,
                     gettext_domain=GETTEXT_DOMAIN,
                     paths=Paths)
