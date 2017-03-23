# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2007 Johan Dahlin
#
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

import overrides_hack
overrides_hack

from common import TestCase

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst
Gst.init(None)

R = Gst.DoubleRange

class TestDoubleRange(TestCase):
    def testConstructor(self):
        Gst.init(None)

        r = R(1.2, 3.4)
        self.assertEquals(r.start, 1.2)
        self.assertEquals(r.stop, 3.4)
        self.assertRaises(TypeError, R, {}, 2)
        self.assertRaises(TypeError, R, 2, ())
        self.assertRaises(TypeError, R, 2, 1)
        self.assertRaises(TypeError, R)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(R(1,2)), '<Gst.DoubleRange [1.0,2.0]>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = R(1,2)
        value = st["range"]

        self.failUnlessEqual(value.start, 1.0)
        self.failUnlessEqual(value.stop, 2.0)
