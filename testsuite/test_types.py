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
import unittest, sys

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst
Gst.init(None)

Gst.DoubleRange = Gst.DoubleRange

class TestDoubleRange(TestCase):
    def testConstructor(self):
        Gst.init(None)

        Gst.DoubleRange = Gst.DoubleRange(1.2, 3.4)
        self.assertEquals(r.start, 1.2)
        self.assertEquals(r.stop, 3.4)
        self.assertRaises(TypeError, Gst.DoubleRange, {}, 2)
        self.assertRaises(TypeError, Gst.DoubleRange, 2, ())
        self.assertRaises(TypeError, Gst.DoubleRange, 2, 1)
        self.assertRaises(TypeError, Gst.DoubleRange)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.DoubleRange(1,2)), '<Gst.DoubleRange [1.0,2.0]>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = Gst.DoubleRange(1,2)
        value = st["range"]

        self.failUnlessEqual(value.start, 1.0)
        self.failUnlessEqual(value.stop, 2.0)


class TestFraction(TestCase):
    def testConstructor(self):
        Gst.init(None)

        frac = Gst.Fraction(1, 2)
        self.assertEquals(frac.num, 1)
        self.assertEquals(frac.denom, 2)

        frac = Gst.Fraction(1)
        self.assertEquals(frac.num, 1)
        self.assertEquals(frac.denom, 1)

        self.assertRaises(TypeError, Gst.Fraction)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.Fraction(1, 2)), '<Gst.Fraction 1/2>')

    def testEqNe(self):
        Gst.init(None)

        frac = Gst.Fraction(1, 2)
        self.assertEquals(frac, frac)
        self.assertEquals(Gst.Fraction(1, 2), Gst.Fraction(1, 2))
        self.assertEquals(Gst.Fraction(2, 4), Gst.Fraction(1, 2))

        self.assertNotEquals(Gst.Fraction(1, 3), Gst.Fraction(1, 2))
        self.assertNotEquals(Gst.Fraction(2, 1), Gst.Fraction(1, 2))

    def testMul(self):
        Gst.init(None)

        self.assertEquals(Gst.Fraction(1, 2) * Gst.Fraction(1, 2), Gst.Fraction(1, 4))
        self.assertEquals(Gst.Fraction(2, 3) * Gst.Fraction(4, 5), Gst.Fraction(8, 15))
        self.assertEquals(Gst.Fraction(1, 3) * Gst.Fraction(4), Gst.Fraction(4, 3))
        self.assertEquals(Gst.Fraction(1, 3) * 4, Gst.Fraction(4, 3))

    def testRMul(self):
        Gst.init(None)

        self.assertEquals(2 * Gst.Fraction(1, 2), Gst.Fraction(1))
        self.assertEquals(4 * Gst.Fraction(1, 2), Gst.Fraction(2))
        self.assertEquals(-10 * Gst.Fraction(1, 2), Gst.Fraction(-5))

    def testDiv(self):
        Gst.init(None)

        self.assertEquals(Gst.Fraction(1, 3) / Gst.Fraction(1, 4), Gst.Fraction(4, 3))
        self.assertEquals(Gst.Fraction(2, 3) / Gst.Fraction(4, 5), Gst.Fraction(10, 12))

        self.assertEquals(Gst.Fraction(1, 3) / Gst.Fraction(4), Gst.Fraction(1, 12))
        self.assertEquals(Gst.Fraction(1, 3) / 4, Gst.Fraction(1, 12))
        self.assertEquals(Gst.Fraction(1, 3) / 2, Gst.Fraction(1, 6))
        self.assertEquals(Gst.Fraction(1, 5) / -4, Gst.Fraction(1, -20))

    def testRDiv(self):
        Gst.init(None)

        self.assertEquals(2 / Gst.Fraction(1, 3), Gst.Fraction(6, 1))
        self.assertEquals(-4 / Gst.Fraction(1, 5), Gst.Fraction(-20, 1))

    def testFloat(self):
        Gst.init(None)

        self.assertEquals(float(Gst.Fraction(1, 2)), 0.5)

    def testPropertyMarshalling(self):
        Gst.init(None)

        obj = Gst.ElementFactory.make("rawvideoparse")
        if not obj:
            obj = Gst.ElementFactory.make("rawvideoparse")

        if not obj:
            # no (raw)videoparse and I don't know of any elements in core or -base using
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


class TestFractionRange(TestCase):
    def testConstructor(self):
        Gst.init(None)

        r = Gst.FractionRange(Gst.Fraction(1, 30), Gst.Fraction(1, 2))
        self.assertEquals(r.start, Gst.Fraction(1, 30))
        self.assertEquals(r.stop, Gst.Fraction(1, 2))
        self.assertRaises(TypeError, Gst.FractionRange, Gst.Fraction(1, 2), Gst.Fraction(1, 30))
        self.assertRaises(TypeError, Gst.FractionRange, 2, Gst.Fraction(1, 2))
        self.assertRaises(TypeError, Gst.FractionRange, Gst.Fraction(1, 2), 2)
        self.assertRaises(TypeError, Gst.FractionRange)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.FractionRange(Gst.Fraction(1,30), Gst.Fraction(1,2))),
                '<Gst.FractionRange [1/30,1/2]>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = Gst.FractionRange(Gst.Fraction(1, 30), Gst.Fraction(1, 2))
        value = st["range"]

        self.failUnlessEqual(value.start, Gst.Fraction(1, 30))
        self.failUnlessEqual(value.stop, Gst.Fraction(1, 2))

class TestDoubleRange(TestCase):
    def testConstructor(self):
        Gst.init(None)

        r = Gst.DoubleRange(1.2, 3.4)
        self.assertEquals(r.start, 1.2)
        self.assertEquals(r.stop, 3.4)
        self.assertRaises(TypeError, Gst.DoubleRange, {}, 2)
        self.assertRaises(TypeError, Gst.DoubleRange, 2, ())
        self.assertRaises(TypeError, Gst.DoubleRange, 2, 1)
        self.assertRaises(TypeError, Gst.DoubleRange)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.DoubleRange(1,2)), '<Gst.DoubleRange [1.0,2.0]>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = Gst.DoubleRange(1,2)
        value = st["range"]

        self.failUnlessEqual(value.start, 1.0)
        self.failUnlessEqual(value.stop, 2.0)


class TestInt64Range(TestCase):
    @unittest.skipUnless(sys.version_info >= (3, 0), "requires Python 3")
    def testConstructor(self):
        Gst.init(None)

        r = Gst.Int64Range(range(0, 10, 2))
        self.assertEquals(r.range, range(0, 10, 2))
        self.assertRaises(TypeError, Gst.Int64Range, range(1, 10, 2))
        self.assertRaises(TypeError, Gst.Int64Range, range(0, 9, 2))
        self.assertRaises(TypeError, Gst.Int64Range, range(10, 0))
        self.assertRaises(TypeError, Gst.Int64Range, 1)
        self.assertRaises(TypeError, Gst.Int64Range)

    @unittest.skipUnless(sys.version_info >= (3, 0), "requires Python 3")
    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.Int64Range(range(0, 10, 2))), '<Gst.Int64Range [0,10,2]>')

    @unittest.skipUnless(sys.version_info >= (3, 0), "requires Python 3")
    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = Gst.Int64Range(range(0, 10, 2))
        value = st["range"]

        self.failUnlessEqual(value, range(0, 10, 2))


class TestValueArray(TestCase):
    def testConstructor(self):
        Gst.init(None)

        a = Gst.ValueArray((1,2,3))
        self.assertEquals(a.array, [1,2,3])

        self.assertRaises(TypeError, Gst.ValueArray, 1)
        self.assertRaises(TypeError, Gst.ValueArray)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.ValueArray([1,2,3])), '<Gst.ValueArray <1,2,3>>')

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

        obj.props.plane_strides = Gst.ValueArray([640,320,320])

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
        st["array"] = Gst.ValueArray([Gst.Fraction(1, 30), Gst.Fraction(1, 2)])
        value = st["array"]
        st["array"] = Gst.ValueArray(value)

        self.failUnlessEqual(value[0], Gst.Fraction(1, 30))
        self.failUnlessEqual(value[1], Gst.Fraction(1, 2))

        st["matrix"] = Gst.ValueArray([Gst.ValueArray([0, 1]), Gst.ValueArray([-1, 0])])
        value = st["matrix"]

        self.failUnlessEqual(value[0][0], 0)
        self.failUnlessEqual(value[0][1], 1)
        self.failUnlessEqual(value[1][0], -1)
        self.failUnlessEqual(value[1][1], 0)


class TestValueList(TestCase):
    def testConstructor(self):
        Gst.init(None)

        a = Gst.ValueList((1,2,3))
        self.assertEquals(a.array, [1,2,3])

        self.assertRaises(TypeError, Gst.ValueList, 1)
        self.assertRaises(TypeError, Gst.ValueList)

    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.ValueList([1,2,3])), '<Gst.ValueList {1,2,3}>')

    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["framerate"] = Gst.ValueList([Gst.Fraction(1, 30), Gst.Fraction(1, 2)])
        value = st["framerate"]

        self.failUnlessEqual(value[0], Gst.Fraction(1, 30))
        self.failUnlessEqual(value[1], Gst.Fraction(1, 2))

        st["matrix"] = Gst.ValueList([Gst.ValueList([0, 1]), Gst.ValueList([-1 ,0])])
        value = st["matrix"]

        self.failUnlessEqual(value[0][0], 0)
        self.failUnlessEqual(value[0][1], 1)
        self.failUnlessEqual(value[1][0], -1)
        self.failUnlessEqual(value[1][1], 0)

class TestIntRange(TestCase):
    @unittest.skipUnless(sys.version_info >= (3, 0), "requires Python 3")
    def testConstructor(self):
        Gst.init(None)

        r = Gst.IntRange(range(0, 10, 2))
        self.assertEquals(r.range, range(0, 10, 2))
        self.assertRaises(TypeError, Gst.IntRange, range(1, 10, 2))
        self.assertRaises(TypeError, Gst.IntRange, range(0, 9, 2))
        self.assertRaises(TypeError, Gst.IntRange, range(10, 0))
        self.assertRaises(TypeError, Gst.IntRange, 1)
        self.assertRaises(TypeError, Gst.IntRange)

    @unittest.skipUnless(sys.version_info >= (3, 0), "requires Python 3")
    def testRepr(self):
        Gst.init(None)

        self.assertEquals(repr(Gst.IntRange(range(0, 10, 2))), '<Gst.IntRange [0,10,2]>')

    @unittest.skipUnless(sys.version_info >= (3, 0), "requires Python 3")
    def testGetValue(self):
        Gst.init(None)

        st = Gst.Structure.new_empty("video/x-raw")
        st["range"] = Gst.IntRange(range(0, 10, 2))
        value = st["range"]

        self.failUnlessEqual(value, range(0, 10, 2))
