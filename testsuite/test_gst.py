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

from common import gst, TestCase, unittest

class TimeArgsTest(TestCase):
    def testNoneTime(self):
        self.assertRaises(TypeError, gst.TIME_ARGS, None)

    def testStringTime(self):
        self.assertRaises(TypeError, gst.TIME_ARGS, "String")

    def testClockTimeNone(self):
        self.assertEquals(gst.TIME_ARGS(gst.CLOCK_TIME_NONE), 'CLOCK_TIME_NONE')

    def testOneSecond(self):
        self.assertEquals(gst.TIME_ARGS(gst.SECOND), '0:00:01.000000000')

if __name__ == "__main__":
    unittest.main()
