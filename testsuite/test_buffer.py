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
        assert hasattr(buffer, 'size')
        assert buffer.size == len(buffer)
        
    def testBufferCreateSub(self):
        s = ''
        for i in range(64):
            s += '%02d' % i
            
        buffer = gst.Buffer(s)
        assert len(buffer) == 128

        sub = buffer.create_sub(16, 16)
        assert sub.offset == gst.CLOCK_TIME_NONE, sub.offset

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


if __name__ == "__main__":
    unittest.main()
