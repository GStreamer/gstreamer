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
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

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
