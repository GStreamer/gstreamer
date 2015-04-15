# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
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

from common import gst, TestCase
from gst import tag

class TesLibTag(TestCase):
    def testXmp(self):
        taglist = gst.TagList()
        taglist['title'] = 'my funny title'
        taglist['geo-location-latitude'] = 23.25

        xmp = tag.tag_list_to_xmp_buffer (taglist, True)
        self.assertNotEquals(xmp, None)
        taglist2 = tag.tag_list_from_xmp_buffer (xmp)

        self.assertEquals(len(taglist2), 2)
        self.assertEquals(taglist2['title'], 'my funny title')
        self.assertEquals(taglist2['geo-location-latitude'], 23.25)

