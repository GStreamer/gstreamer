# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# Copyright (C) 2009 Thomas Vander Stichele
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

import sys
import overrides_hack
overrides_hack
from common import TestCase, unittest
from gi.repository import Gst


class TimeArgsTest(TestCase):
    def testNoneTime(self):
        self.assertRaises(TypeError, Gst.TIME_ARGS, None)

    def testStringTime(self):
        self.assertRaises(TypeError, Gst.TIME_ARGS, "String")

    def testClockTimeNone(self):
        self.assertEqual(Gst.TIME_ARGS(Gst.CLOCK_TIME_NONE), 'CLOCK_TIME_NONE')

    def testOneSecond(self):
        self.assertEqual(Gst.TIME_ARGS(Gst.SECOND), '0:00:01.000000000')


class TestNotInitialized(TestCase):
    def testNotInitialized(self):
        if sys.version_info >= (3, 0):
            assert_type = Gst.NotInitialized
        else:
            assert_type = TypeError

        with self.assertRaises(assert_type):
            Gst.Caps.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.Structure.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.ElementFactory.make("identity", None)

    def testNotDeinitialized(self):
        Gst.init(None)

        assert(Gst.Caps.from_string("audio/x-raw"))
        assert(Gst.Structure.from_string("audio/x-raw"))
        assert(Gst.ElementFactory.make("identity", None))

        Gst.deinit()
        if sys.version_info >= (3, 0):
            assert_type = Gst.NotInitialized
        else:
            assert_type = TypeError

        with self.assertRaises(assert_type):
            Gst.Caps.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.Structure.from_string("audio/x-raw")

        with self.assertRaises(assert_type):
            Gst.ElementFactory.make("identity", None)


class TestStructure(TestCase):

    def test_new(self):
        Gst.init(None)
        test = Gst.Structure('test', test=1)
        self.assertEqual(test['test'], 1)

        test = Gst.Structure('test,test=1')
        self.assertEqual(test['test'], 1)


class TestBin(TestCase):

    def test_add_pad(self):
        Gst.init(None)
        self.assertEqual(Gst.ElementFactory.make("bin", None).sinkpads, [])


class TestBufferMap(TestCase):

    def test_map_unmap_manual(self):
        Gst.init(None)
        buf = Gst.Buffer.new_wrapped([42])
        info = buf.map(Gst.MapFlags.READ | Gst.MapFlags.WRITE)
        self.assertEqual(info.data[0], 42)
        buf.unmap(info)
        with self.assertRaises(ValueError):
            info.data[0]

    def test_map_unmap_context(self):
        Gst.init(None)
        buf = Gst.Buffer.new_wrapped([42])
        with buf.map(Gst.MapFlags.READ | Gst.MapFlags.WRITE) as info:
            self.assertEqual(info.data[0], 42)
        with self.assertRaises(ValueError):
            info.data[0]


if __name__ == "__main__":
    unittest.main()
