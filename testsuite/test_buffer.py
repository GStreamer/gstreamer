#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>

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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import sys
import gc
from common import gobject, gst, unittest

class BufferTest(unittest.TestCase):
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
        #assert hasattr(buffer, 'size')
        #assert buffer.size == len(buffer)
        
    def testBufferMaxSize(self):
        buffer = gst.Buffer(buffer_size=16)
        assert hasattr(buffer, 'maxsize')
        assert buffer.maxsize == 16

    def testBufferCreateSub(self):
        s = ''
        for i in range(64):
            s += '%02d' % i
            
        buffer = gst.Buffer(s)
        assert len(buffer) == 128

        sub = buffer.create_sub(16, 16)
        assert sub.maxsize == 16
        assert sub.offset == -1, sub.offset

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

    def testBufferFlagIsSet(self):
        buffer = gst.Buffer()
        # Off by default
        assert not buffer.flag_is_set(gst.BUFFER_READONLY)

        # Try switching on and off
        buffer.flag_set(gst.BUFFER_READONLY)
        assert buffer.flag_is_set(gst.BUFFER_READONLY)
        buffer.flag_unset(gst.BUFFER_READONLY)
        assert not buffer.flag_is_set(gst.BUFFER_READONLY)

        # Try switching on and off
        buffer.flag_set(gst.BUFFER_IN_CAPS)
        assert buffer.flag_is_set(gst.BUFFER_IN_CAPS)
        buffer.flag_unset(gst.BUFFER_IN_CAPS)
        assert not buffer.flag_is_set(gst.BUFFER_IN_CAPS)

    def testAttrType(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "data_type")
        # XXX: Expose this in gobject
        #assert isinstance(buffer.data_type, gobject.GType)
        assert buffer.data_type == buffer.__gtype__
        
    def testAttrFlags(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "flags")
        assert isinstance(buffer.flags, int)
 
    def testAttrTimestamp(self):
        buffer = gst.Buffer()
        assert hasattr(buffer, "timestamp")
        assert isinstance(buffer.timestamp, int)

        assert buffer.timestamp == -1
        buffer.timestamp = 0
        assert buffer.timestamp == 0
        
if __name__ == "__main__":
    unittest.main()
