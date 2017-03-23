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

A = Gst.ValueArray

class TestFraction(TestCase):
    def testConstructor(self):
        Gst.init(None)

        a = A((1,2,3))
        self.assertEquals(a.array, [1,2,3])

        self.assertRaises(TypeError, A, 1)
        self.assertRaises(TypeError, A)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(A([1,2,3])), '<Gst.ValueArray <1,2,3>>')

    def testPropertyMarshalling(self):
        Gst.init(None)

        obj = Gst.ElementFactory.make("rawvideoparse")

        if not obj:
            # no rawvideoparse and I don't know of any elements in core or -base using
            # fraction properties. Skip this test.
            return

        value = obj.props.plane_strides
        self.failUnlessEqual(value[0], 320)
        self.failUnlessEqual(value[1], 160)
        self.failUnlessEqual(value[2], 160)

        obj.props.plane_strides = A([640,320,320])

        value = obj.props.plane_strides
        self.failUnlessEqual(value[0], 640)
        self.failUnlessEqual(value[1], 320)
        self.failUnlessEqual(value[2], 320)

        def bad():
            obj.props.plane_strides = 1
        self.failUnlessRaises(TypeError, bad)

        value = obj.props.plane_strides
        self.failUnlessEqual(value[0], 640)
        self.failUnlessEqual(value[1], 320)
        self.failUnlessEqual(value[2], 320)

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["array"] = A([Gst.Fraction(1, 30), Gst.Fraction(1, 2)])
        value = st["array"]

        self.failUnlessEqual(value[0], Gst.Fraction(1, 30))
        self.failUnlessEqual(value[1], Gst.Fraction(1, 2))
