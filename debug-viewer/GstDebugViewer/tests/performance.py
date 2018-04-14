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

"""GStreamer Debug Viewer performance test program."""

import sys
import os
import os.path
from glob import glob
import time

import gi

from gi.repository import GObject

from .. import Common, Data, GUI


class TestParsingPerformance (object):

    def __init__(self, filename):

        self.main_loop = GObject.MainLoop()
        self.log_file = Data.LogFile(filename, Common.Data.DefaultDispatcher())
        self.log_file.consumers.append(self)

    def start(self):

        self.log_file.start_loading()

    def handle_load_started(self):

        self.start_time = time.time()

    def handle_load_finished(self):

        diff = time.time() - self.start_time
        print("line cache built in %0.1f ms" % (diff * 1000.,))

        start_time = time.time()
        model = GUI.LazyLogModel(self.log_file)
        for row in model:
            pass
        diff = time.time() - start_time
        print("model iterated in %0.1f ms" % (diff * 1000.,))
        print("overall time spent: %0.1f s" % (time.time() - self.start_time,))

        import resource
        rusage = resource.getrusage(resource.RUSAGE_SELF)
        print("time spent in user mode: %.2f s" % (rusage.ru_utime,))
        print("time spent in system mode: %.2f s" % (rusage.ru_stime,))


def main():

    if len(sys.argv) > 1:
        test = TestParsingPerformance(sys.argv[1])
        test.start()


if __name__ == "__main__":
    main()
