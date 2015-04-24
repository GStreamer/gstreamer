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
        self.assertEquals(Gst.TIME_ARGS(Gst.CLOCK_TIME_NONE), 'CLOCK_TIME_NONE')

    def testOneSecond(self):
        self.assertEquals(Gst.TIME_ARGS(Gst.SECOND), '0:00:01.000000000')

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


if __name__ == "__main__":
    unittest.main()
