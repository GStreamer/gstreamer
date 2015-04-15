# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2009 Edward Hervey
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

from common import gobject, gst, unittest, TestCase

class AdapterTest(TestCase):

    def setUp(self):
        TestCase.setUp(self)
        self.adapter = gst.Adapter()

    def tearDown(self):
        self.adapter = None
        TestCase.tearDown(self)

    def testAvailable(self):
        # starts empty
        self.assertEquals(self.adapter.available(), 0)
        self.assertEquals(self.adapter.available_fast(), 0)

        # let's give it 4 bytes
        self.adapter.push(gst.Buffer("1234"))
        self.assertEquals(self.adapter.available_fast(), 4)

        # let's give it another 5 bytes
        self.adapter.push(gst.Buffer("56789"))
        # we now have 9 bytes
        self.assertEquals(self.adapter.available(), 9)
        # but can only do a fast take of 4 bytes (the first buffer)
        self.assertEquals(self.adapter.available_fast(), 4)

    def testPeek(self):
        self.adapter.push(gst.Buffer("0123456789"))

        # let's peek at 5 bytes
        b = self.adapter.peek(5)
        # it can return more than 5 bytes
        self.assert_(len(b) >= 5)
        self.assertEquals(b, "01234")

        # it's still 10 bytes big
        self.assertEquals(self.adapter.available(), 10)

        # if we try to peek more than what's available, we'll have None
        self.assertEquals(self.adapter.peek(11), None)

    def testFlush(self):
        self.adapter.push(gst.Buffer("0123456789"))
        self.assertEquals(self.adapter.available(), 10)

        self.adapter.flush(5)
        self.assertEquals(self.adapter.available(), 5)

        # it flushed the first 5 bytes
        self.assertEquals(self.adapter.peek(5), "56789")

        self.adapter.flush(5)
        self.assertEquals(self.adapter.available(), 0)

    def testTake(self):
        self.adapter.push(gst.Buffer("0123456789"))
        self.assertEquals(self.adapter.available(), 10)

        s = self.adapter.take(5)
        self.assertEquals(s, "01234")
        self.assertEquals(self.adapter.available(), 5)
