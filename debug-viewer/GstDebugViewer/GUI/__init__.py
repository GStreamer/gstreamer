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

"""GStreamer Debug Viewer GUI module."""

__author__ = u"René Stadler <mail@renestadler.de>"
__version__ = "0.1"

import gi

from GstDebugViewer.GUI.app import App


def main(args):

    app = App()

    # TODO: Once we support more than one window, open one window for each
    # supplied filename.
    window = app.windows[0]
    if len(args) > 0:
        window.set_log_file(args[0])

    app.run()


if __name__ == "__main__":
    main()
