#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>
#               2005 Thomas Vander Stichele <thomas at apestaart dot org>

# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import os
import sys

import tempfile

from common import gst, unittest

class ErrorTest(unittest.TestCase):
    def setUp(self):
        fd, self.name = tempfile.mkstemp(prefix='tmpgsttest')
        os.close(fd)
        os.unlink(self.name)

    def testFileSrc(self):
        pipe = gst.parse_launch("filesrc location=%s ! fakesink" % self.name)
        pipe.connect('error', self._error_cb)
        pipe.set_state(gst.STATE_PLAYING)
        pipe.iterate()
        self.failUnless(self.errored)
        self.assertEquals(self.element, pipe)
        # FIXME: GError should be wrapped in pygtk
        self.assertEquals(self.error.__class__, gst.GError)

    def _error_cb(self, element, source, error, debug):
        self.errored = True
        self.element = element
        self.source = source
        self.error = error
        self.debug = debug
        

if __name__ == "__main__":
    unittest.main()
