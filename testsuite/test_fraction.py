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
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

import gobject
gobject.threads_init()
from common import gst, TestCase

F = gst.Fraction

class TestFraction(TestCase):
    def testConstructor(self):
        frac = F(1, 2)
        self.assertEquals(frac.num, 1)
        self.assertEquals(frac.denom, 2)

        frac = F(1)
        self.assertEquals(frac.num, 1)
        self.assertEquals(frac.denom, 1)

        self.assertRaises(TypeError, F)

    def testRepr(self):
        self.assertEquals(repr(F(1, 2)), '<gst.Fraction 1/2>')

    def testEqNe(self):
        frac = F(1, 2)
        self.assertEquals(frac, frac)
        self.assertEquals(F(1, 2), F(1, 2))
        self.assertEquals(F(2, 4), F(1, 2))

        self.assertNotEquals(F(1, 3), F(1, 2))
        self.assertNotEquals(F(2, 1), F(1, 2))

    def testMul(self):
        self.assertEquals(F(1, 2) * F(1, 2), F(1, 4))
        self.assertEquals(F(2, 3) * F(4, 5), F(8, 15))
        self.assertEquals(F(1, 3) * F(4), F(4, 3))
        self.assertEquals(F(1, 3) * 4, F(4, 3))

    def testRMul(self):
        self.assertEquals(2 * F(1, 2), F(1))
        self.assertEquals(4 * F(1, 2), F(2))
        self.assertEquals(-10 * F(1, 2), F(-5))

    def testDiv(self):
        self.assertEquals(F(1, 3) / F(1, 4), F(4, 3))
        self.assertEquals(F(2, 3) / F(4, 5), F(10, 12))

        self.assertEquals(F(1, 3) / F(4), F(1, 12))
        self.assertEquals(F(1, 3) / 4, F(1, 12))
        self.assertEquals(F(1, 3) / 2, F(1, 6))
        self.assertEquals(F(1, 5) / -4, F(1, -20))

    def testRDiv(self):
        self.assertEquals(2 / F(1, 3), F(6, 1))
        self.assertEquals(-4 / F(1, 5), F(-20, 1))

    def testFloat(self):
        self.assertEquals(float(F(1, 2)), 0.5)

    def testPropertyMarshalling(self):
        try:
            obj = gst.element_factory_make("videoparse")
        except gst.ElementNotFoundError:
            # no videoparse and I don't know of any elements in core or -base using
            # fraction properties. Skip this test.
            return
        value = obj.props.framerate
        self.failUnlessEqual(value.num, 25)
        self.failUnlessEqual(value.denom, 1)

        obj.props.framerate = gst.Fraction(2, 1)
        value = obj.props.framerate
        self.failUnlessEqual(value.num, 2)
        self.failUnlessEqual(value.denom, 1)

        def bad():
            obj.props.framerate = 1
        self.failUnlessRaises(TypeError, bad)

        value = obj.props.framerate
        self.failUnlessEqual(value.num, 2)
        self.failUnlessEqual(value.denom, 1)
