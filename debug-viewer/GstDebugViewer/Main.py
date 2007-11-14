# -*- coding: utf-8; mode: python; -*-
#
#  GStreamer Debug Viewer
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

"""GStreamer debug viewer main module"""

import sys
from gettext import gettext as _, ngettext

import GstDebugViewer.Common.Main
Common = GstDebugViewer.Common

GETTEXT_DOMAIN = "gst-debug-viewer"

def main_version ():

    from GstDebugViewer import version

    print "GStreamer Debug Viewer %s" % (version,)

class Paths (Common.Main.PathsProgramBase):

    program_name = "gst-debug-viewer"

class OptionParser (Common.Main.LogOptionParser):

    def __init__ (self, options):

        Common.Main.LogOptionParser.__init__ (self, options)

        options["main"] = None

        self.add_option ("version", None, _("Display version and exit"))

    def get_parameter_string (self):

        return _("- Display and analyze debug log files")

    def handle_parse_complete (self):

        try:
            version = self.options["version"]
        except KeyError:
            pass
        else:
            main_version ()
            sys.exit (0)

        if self.options["main"] is None:
            import GUI
            self.options["main"] = GUI.main

def main ():

    options = {}
    parser = OptionParser (options)

    Common.Main.main (option_parser = parser,
                      gettext_domain = GETTEXT_DOMAIN,
                      paths = Paths)
