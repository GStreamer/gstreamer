# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
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
import gc
from common import gobject, gst, unittest, TestCase

class BufferTest(TestCase):
    def testBufferBuffer(self):
        buf = gst.Buffer('test')
        assert str(buffer(buf)) == 'test'

    def testBufferStr(self):
        buffer = gst.Buffer('test')
        assert str(buffer) == 'test'

    def testBufferAlloc(self):
        bla = 'mooooooo'
        buffer = gst.Buffer(bla + '12345')
        gc.collect ()
        assert str(buffer) == 'mooooooo12345'
		
    def testBufferBadConstructor(self):
        self.assertRaises(TypeError, gst.Buffer, 'test', 0)
        
    def testBufferStrNull(self):
        test_string = 't\0e\0s\0t\0'
        buffer = gst.Buffer(test_string)
        assert str(buffer) == test_string

    def testBufferSize(self):
        test_string = 'a little string'
        buffer = gst.Buffer(test_string)
        assert len(buffer) == len(test_string)
        assert hasattr(buffer, 'size')
        assert buffer.size == len(buffer)
        
    def testBufferCreateSub(self):
        s = ''
        for i in range(64):
            s += '%02d' % i
            
        buffer = gst.Buffer(s)
        self.assertEquals(len(buffer), 128)

        sub = buffer.create_sub(16, 16)
        self.assertEquals(sub.size, 16)
        self.assertEquals(sub.data, buffer.data[16:32])
        self.assertEquals(sub.offset, gst.CLOCK_TIME_NONE)

    def testBufferMerge(self):
        buffer1 = gst.Buffer('foo')
        buffer2 = gst.Buffer('bar')

        merged_buffer = buffer1.merge(buffer2)
        assert str(merged_buffer) == 'foobar'
        
    def testBufferJoin(self):
        buffer1 = gst.Buffer('foo')
        buffer2 = gst.Buffer('bar')

        joined_buffer = buffer1.merge(buffer2)
        assert str(joined_buffer) == 'foobar'
        
    def testBufferSpan(self):
        buffer1 = gst.Buffer('foo')
        buffer2 = gst.Buffer('bar')

        spaned_buffer = buffer1.span(0L, buffer2, 6L)
        assert str(spaned_buffer) == 'foobar'
    def testBufferCopyOnWrite(self):
        s='test_vector'
        buffer = gst.Buffer(s)
        sub = buffer.create_sub(0, buffer.size)
        self.assertEquals(sub.size, buffer.size)
        out = sub.copy_on_write ()
        self.assertEquals(out.size, sub.size)
        assert str(out) == str(buffer)
        out[5] = 'w'
        assert str(out) == 'test_wector'

    def testBufferFlagIsSet(self):
        buffer = gst.Buffer()
        # Off by default
        assert not buffer.flag_is_set(gst.BUFFER_FLAG_READONLY)

        # Try switching on and off
        buffer.flag_set(gst.BUFFER_FLAG_READONLY)
        assert buffer.flag_is_set(gst.BUFFER_FLAG_READONLY)
        buffer.flag_unset(gst.BUFFER_FLAG_READONLY)
        assert not buffer.flag_is_set(gst.BUFFER_FLAG_READONLY)

        # Try switching on and off
        buffer.flag_set(gst.BUFFER_FLAG_IN_CAPS)
        assert buffer.flag_is_set(gst.BUFFER_FLAG_IN_CAPS)
        buffer.flag_unset(gst.BUFFER_FLAG_IN_CAPS)
        assert not buffer.flag_is_set(gst.BUFFER_FLAG_IN_CAPS)

    def testAttrFlags(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "flags")
        assert isinstance(buffer.flags, int)
 
    def testAttrTimestamp(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "timestamp")
        assert isinstance(buffer.timestamp, long)

        assert buffer.timestamp == gst.CLOCK_TIME_NONE
        buffer.timestamp = 0
        assert buffer.timestamp == 0
        buffer.timestamp = 2**64 - 1
        assert buffer.timestamp == 2**64 - 1

    def testAttrDuration(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "duration")
        assert isinstance(buffer.duration, long)

        assert buffer.duration == gst.CLOCK_TIME_NONE
        buffer.duration = 0
        assert buffer.duration == 0
        buffer.duration = 2**64 - 1
        assert buffer.duration == 2**64 - 1
        
    def testAttrOffset(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "offset")
        assert isinstance(buffer.offset, long)

        assert buffer.offset == gst.CLOCK_TIME_NONE
        buffer.offset = 0
        assert buffer.offset == 0
        buffer.offset = 2**64 - 1
        assert buffer.offset == 2**64 - 1

    def testAttrOffset_end(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "offset_end")
        assert isinstance(buffer.offset_end, long)

        assert buffer.offset_end == gst.CLOCK_TIME_NONE
        buffer.offset_end = 0
        assert buffer.offset_end == 0
        buffer.offset_end = 2**64 - 1
        assert buffer.offset_end == 2**64 - 1

    def testBufferCaps(self):
        buffer = gst.Buffer()
        caps = gst.caps_from_string('foo/blah')
        gst.info("before settings caps")
        buffer.set_caps(caps)
        gst.info("after settings caps")
        c = buffer.get_caps()
        gst.info("after getting caps")
        self.assertEquals(caps, c)

if __name__ == "__main__":
    unittest.main()
