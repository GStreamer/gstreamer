# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2006 Edward Hervey <edward@fluendo.com>
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

from common import gst, unittest, TestCase

class SegmentTest(TestCase):
    def testSeekNoSize(self):
        segment = gst.Segment()
        segment.init(gst.FORMAT_BYTES)

        # configure segment to start at 100 with no defined stop position
        update = segment.set_seek(1.0, gst.FORMAT_BYTES, gst.SEEK_FLAG_NONE,
                                  gst.SEEK_TYPE_SET, 100,
                                  gst.SEEK_TYPE_NONE, -1)
        self.assertEquals(update, True)
        self.assertEquals(segment.start, 100)
        self.assertEquals(segment.stop, -1)

        # configure segment to stop relative, should not do anything since
        # size is unknown
        update = segment.set_seek(1.0, gst.FORMAT_BYTES, gst.SEEK_FLAG_NONE,
                                  gst.SEEK_TYPE_NONE, 200,
                                  gst.SEEK_TYPE_CUR, -100)

        # the update flag is deprecated, we cannot check for proper behaviour.
        #self.assertEquals(update, False)
        self.assertEquals(segment.start, 100)
        self.assertEquals(segment.stop, -1)

        # clipping on outside range, always returns False
        res, cstart, cstop = segment.clip(gst.FORMAT_BYTES, 0, 50)
        self.assertEquals(res, False)

        # touching lower bound but outside
        res, cstart, cstop = segment.clip(gst.FORMAT_BYTES, 50, 100)
        self.assertEquals(res, False)

        # partially inside
        res, cstart, cstop = segment.clip(gst.FORMAT_BYTES, 50, 150)
        self.assertEquals(res, True)
        self.assertEquals(cstart, 100)
        self.assertEquals(cstop, 150)

if __name__ == "__main__":
    unittest.main()
