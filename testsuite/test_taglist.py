# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2007 Johan Dahlin
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


class TestTagList(TestCase):
    def testContains(self):
        taglist = gst.TagList()
        self.failIf('key' in taglist)
        taglist['key'] = 'value'
        self.failUnless('key' in taglist)

    def testLength(self):
        taglist = gst.TagList()
        self.assertEqual(len(taglist), 0)
        taglist['key1'] = 'value'
        taglist['key2'] = 'value'
        self.assertEqual(len(taglist), 2)

    def testKeys(self):
        taglist = gst.TagList()
        self.assertEqual(taglist.keys(), [])
        taglist['key1'] = 'value'
        taglist['key2'] = 'value'
        keys = taglist.keys()
        keys.sort()
        self.assertEqual(keys, ['key1', 'key2'])

    def testUnicode(self):
        taglist = gst.TagList()

        # normal ASCII text
        taglist[gst.TAG_ARTIST] = 'Artist'
        self.failUnless(isinstance(taglist[gst.TAG_ARTIST], unicode))
        self.assertEquals(taglist[gst.TAG_ARTIST], u'Artist')
        self.assertEquals(taglist[gst.TAG_ARTIST], 'Artist')

        # normal ASCII text as unicode
        taglist[gst.TAG_ARTIST] = u'Artist'
        self.failUnless(isinstance(taglist[gst.TAG_ARTIST], unicode))
        self.assertEquals(taglist[gst.TAG_ARTIST], u'Artist')
        self.assertEquals(taglist[gst.TAG_ARTIST], 'Artist')
        
        # real unicode
        taglist[gst.TAG_ARTIST] = u'S\xc3\xadgur R\xc3\xb3s'
        self.failUnless(isinstance(taglist[gst.TAG_ARTIST], unicode))
        self.assertEquals(taglist[gst.TAG_ARTIST], u'S\xc3\xadgur R\xc3\xb3s')
        
    def testUnsignedInt(self):
        taglist = gst.TagList()
        taglist[gst.TAG_TRACK_NUMBER] = 1
        vorbis = gst.tag.to_vorbis_comments(taglist, gst.TAG_TRACK_NUMBER)
        self.assertEquals(vorbis, ['TRACKNUMBER=1'])
