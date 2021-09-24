# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
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

class Audio(TestCase):

    def testBufferclip(self):
        assert hasattr(gst.audio, "buffer_clip")
        # create a segment
        segment = gst.Segment()
        gst.debug("Created the new segment")
        # we'll put a new segment of 500ms to 1000ms
        segment.set_newsegment(False, 1.0, gst.FORMAT_TIME, 0, -1, 0)
        gst.debug("Initialized the new segment")
        # create a new dummy buffer
        b = gst.Buffer("this is a really useless line")
        gst.debug("Created the buffer")
        # clip... which shouldn't do anything
        b2 = gst.audio.buffer_clip(b, segment, 44100, 8)
        gst.debug("DONE !")
