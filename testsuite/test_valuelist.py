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

L = Gst.ValueList

class TestFraction(TestCase):
    def testConstructor(self):
        Gst.init(None)

        a = L((1,2,3))
        self.assertEquals(a.array, [1,2,3])

        self.assertRaises(TypeError, L, 1)
        self.assertRaises(TypeError, L)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(L([1,2,3])), '<Gst.ValueList {1,2,3}>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["framerate"] = L([Gst.Fraction(1, 30), Gst.Fraction(1, 2)])
        value = st["framerate"]

        self.failUnlessEqual(value[0], Gst.Fraction(1, 30))
        self.failUnlessEqual(value[1], Gst.Fraction(1, 2))

        st["matrix"] = L([L([0, 1]), L([-1 ,0])])
        value = st["matrix"]

        self.failUnlessEqual(value[0][0], 0)
        self.failUnlessEqual(value[0][1], 1)
        self.failUnlessEqual(value[1][0], -1)
        self.failUnlessEqual(value[1][1], 0)
