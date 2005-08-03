#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>

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

from common import gst, unittest

import gobject

class Availability(unittest.TestCase):
    def testXOverlay(self):
        assert hasattr(gst.interfaces, 'XOverlay')
        assert issubclass(gst.interfaces.XOverlay, gobject.GInterface)

    def testMixer(self):
        assert hasattr(gst.interfaces, 'Mixer')
        assert issubclass(gst.interfaces.Mixer, gobject.GInterface)

class FunctionCall(unittest.TestCase):
    def FIXME_testXOverlay(self):
        # obviously a testsuite is not allowed to instantiate this
        # since it needs a running X or will fail.  find some way to
        # deal with that.
        element = gst.element_factory_make('xvimagesink')
        assert isinstance(element, gst.Element)
        assert isinstance(element, gst.interfaces.XOverlay)
        element.set_xwindow_id(0L)
        
if __name__ == "__main__":
    unittest.main()
