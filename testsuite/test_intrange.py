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

R = Gst.IntRange

class TestIntRange(TestCase):
    def testConstructor(self):
        Gst.init(None)

        r = R(range(0, 10, 2))
        self.assertEquals(r.range, range(0, 10, 2))
        self.assertRaises(TypeError, R, range(1, 10, 2))
        self.assertRaises(TypeError, R, range(0, 9, 2))
        self.assertRaises(TypeError, R, range(10, 0))
        self.assertRaises(TypeError, R, 1)
        self.assertRaises(TypeError, R)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(R(range(0, 10, 2))), '<Gst.IntRange [0,10,2]>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = R(range(0, 10, 2))
        value = st["range"]

        self.failUnlessEqual(value, range(0, 10, 2))
