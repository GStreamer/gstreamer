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

from gi.repository import Gst
Gst.init(None)

F = Gst.Fraction

class TestFraction(TestCase):
    def testConstructor(self):
        Gst.init(None)

        frac = F(1, 2)
        self.assertEquals(frac.num, 1)
        self.assertEquals(frac.denom, 2)

        frac = F(1)
        self.assertEquals(frac.num, 1)
        self.assertEquals(frac.denom, 1)

        self.assertRaises(TypeError, F)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(F(1, 2)), '<Gst.Fraction 1/2>')

    def testEqNe(self):
        Gst.init(None)

        frac = F(1, 2)
        self.assertEquals(frac, frac)
        self.assertEquals(F(1, 2), F(1, 2))
        self.assertEquals(F(2, 4), F(1, 2))

        self.assertNotEquals(F(1, 3), F(1, 2))
        self.assertNotEquals(F(2, 1), F(1, 2))

    def testMul(self):
        Gst.init(None)

        self.assertEquals(F(1, 2) * F(1, 2), F(1, 4))
        self.assertEquals(F(2, 3) * F(4, 5), F(8, 15))
        self.assertEquals(F(1, 3) * F(4), F(4, 3))
        self.assertEquals(F(1, 3) * 4, F(4, 3))

    def testRMul(self):
        Gst.init(None)

        self.assertEquals(2 * F(1, 2), F(1))
        self.assertEquals(4 * F(1, 2), F(2))
        self.assertEquals(-10 * F(1, 2), F(-5))

    def testDiv(self):
        Gst.init(None)

        self.assertEquals(F(1, 3) / F(1, 4), F(4, 3))
        self.assertEquals(F(2, 3) / F(4, 5), F(10, 12))

        self.assertEquals(F(1, 3) / F(4), F(1, 12))
        self.assertEquals(F(1, 3) / 4, F(1, 12))
        self.assertEquals(F(1, 3) / 2, F(1, 6))
        self.assertEquals(F(1, 5) / -4, F(1, -20))

    def testRDiv(self):
        Gst.init(None)

        self.assertEquals(2 / F(1, 3), F(6, 1))
        self.assertEquals(-4 / F(1, 5), F(-20, 1))

    def testFloat(self):
        Gst.init(None)

        self.assertEquals(float(F(1, 2)), 0.5)

    def testPropertyMarshalling(self):
        Gst.init(None)

        obj = Gst.ElementFactory.make("videoparse")
        if not obj:
            # no videoparse and I don't know of any elements in core or -base using
            # fraction properties. Skip this test.
            return

        value = obj.props.framerate
        self.failUnlessEqual(value.num, 25)
        self.failUnlessEqual(value.denom, 1)

        obj.props.framerate = Gst.Fraction(2, 1)
        value = obj.props.framerate
        self.failUnlessEqual(value.num, 2)
        self.failUnlessEqual(value.denom, 1)

        def bad():
            obj.props.framerate = 1
        self.failUnlessRaises(TypeError, bad)

        value = obj.props.framerate
        self.failUnlessEqual(value.num, 2)
        self.failUnlessEqual(value.denom, 1)

    def testGetFractionValue(self):
        Gst.init(None)

        st = Gst.Structure.from_string("video/x-raw,framerate=10/1")[0]
        value = st["framerate"]

        self.failUnlessEqual(value.num, 10)
        self.failUnlessEqual(value.denom, 1)
