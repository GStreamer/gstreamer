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

R = Gst.FractionRange

class TestFractionRange(TestCase):
    def testConstructor(self):
        Gst.init(None)

        r = R(Gst.Fraction(1, 30), Gst.Fraction(1, 2))
        self.assertEquals(r.start, Gst.Fraction(1, 30))
        self.assertEquals(r.stop, Gst.Fraction(1, 2))
        self.assertRaises(TypeError, R, Gst.Fraction(1, 2), Gst.Fraction(1, 30))
        self.assertRaises(TypeError, R, 2, Gst.Fraction(1, 2))
        self.assertRaises(TypeError, R, Gst.Fraction(1, 2), 2)
        self.assertRaises(TypeError, R)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(R(Gst.Fraction(1,30), Gst.Fraction(1,2))),
                '<Gst.FractionRange [1/30,1/2]>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = R(Gst.Fraction(1, 30), Gst.Fraction(1, 2))
        value = st["range"]

        self.failUnlessEqual(value.start, Gst.Fraction(1, 30))
        self.failUnlessEqual(value.stop, Gst.Fraction(1, 2))
