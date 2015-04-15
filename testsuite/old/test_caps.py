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
from common import gst, unittest, TestCase

class CapsTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.caps = gst.caps_from_string('video/x-raw-yuv,width=10,framerate=5/1;video/x-raw-rgb,width=15,framerate=10/1')
        self.assertEquals(self.caps.__refcount__, 1)
        self.structure = self.caps[0]
        self.any = gst.Caps("ANY")
        self.assertEquals(self.any.__refcount__, 1)
        self.empty = gst.Caps()
        self.assertEquals(self.empty.__refcount__, 1)

    def testCapsMime(self):
        mime = self.structure.get_name()
        assert mime == 'video/x-raw-yuv'

    def testCapsList(self):
        'check if we can access Caps as a list'
        structure = self.caps[0]
        mime = structure.get_name()
        assert mime == 'video/x-raw-yuv'
        structure = self.caps[1]
        mime = structure.get_name()
        assert mime == 'video/x-raw-rgb'

    def testCapsContainingMiniObjects(self):
        # buffer contains hex encoding of ascii 'abcd'
        caps = gst.Caps("video/x-raw-yuv, buf=(buffer)61626364")
        buf = caps[0]['buf']
        assert isinstance(buf, gst.Buffer)
        assert buf.data == "abcd"

        buf = gst.Buffer("1234")
        caps[0]['buf2'] = buf
        buf2 = caps[0]['buf2']
        assert buf2 == buf

    def testCapsConstructEmpty(self):
        caps = gst.Caps()
        assert isinstance(caps, gst.Caps)

    def testCapsConstructFromString(self):
        caps = gst.Caps('video/x-raw-yuv,width=10')
        assert isinstance(caps, gst.Caps)
        assert len(caps) == 1
        assert isinstance(caps[0], gst.Structure)
        assert caps[0].get_name() == 'video/x-raw-yuv'
        assert isinstance(caps[0]['width'], int)
        assert caps[0]['width'] == 10

    def testCapsConstructFromStructure(self):
        struct = gst.structure_from_string('video/x-raw-yuv,width=10,framerate=[0/1, 25/3]')
        caps = gst.Caps(struct)
        assert isinstance(caps, gst.Caps)
        assert len(caps) == 1
        assert isinstance(caps[0], gst.Structure)
        assert caps[0].get_name() == 'video/x-raw-yuv'
        assert isinstance(caps[0]['width'], int)
        assert caps[0]['width'] == 10
        assert isinstance(caps[0]['framerate'], gst.FractionRange)

    def testCapsConstructFromStructures(self):
        struct1 = gst.structure_from_string('video/x-raw-yuv,width=10')
        struct2 = gst.structure_from_string('video/x-raw-rgb,height=20.0')
        caps = gst.Caps(struct1, struct2)
        assert isinstance(caps, gst.Caps)
        assert len(caps) == 2
        struct = caps[0]
        assert isinstance(struct, gst.Structure), struct
        assert struct.get_name() == 'video/x-raw-yuv', struct.get_name()
        assert struct.has_key('width')
        assert isinstance(struct['width'], int)
        assert struct['width'] == 10
        struct = caps[1]
        assert isinstance(struct, gst.Structure), struct
        assert struct.get_name() == 'video/x-raw-rgb', struct.get_name()
        assert struct.has_key('height')
        assert isinstance(struct['height'], float)
        assert struct['height'] == 20.0

    def testCapsReferenceStructs(self):
        'test that shows why it\'s not a good idea to use structures by reference'
        caps = gst.Caps('hi/mom,width=0')
        structure = caps[0]
        del caps
        assert structure['width'] == 0
        

    def testCapsStructureChange(self):
        'test if changing the structure of the caps works by reference'
        assert self.structure['width'] == 10
        self.structure['width'] = 5
        assert self.structure['width'] == 5.0
        # check if we changed the caps as well
        structure = self.caps[0]
        assert structure['width'] == 5.0

    def testCapsBadConstructor(self):
        struct = gst.structure_from_string('video/x-raw-yuv,width=10')
        self.assertRaises(TypeError, gst.Caps, None)
        self.assertRaises(TypeError, gst.Caps, 1)
        self.assertRaises(TypeError, gst.Caps, 2.0)
        self.assertRaises(TypeError, gst.Caps, object)
        self.assertRaises(TypeError, gst.Caps, 1, 2, 3)
        
        # This causes segfault!
        #self.assertRaises(TypeError, gst.Caps, struct, 10, None)

    def testTrueFalse(self):
        'test that comparisons using caps work the intended way'
        assert self.any # not empty even though it has no structures
        assert not self.empty
        assert not gst.Caps('EMPTY') # also empty
        assert gst.Caps('your/mom')

    def testComparisons(self):
        assert self.empty < self.any
        assert self.empty < self.structure
        assert self.empty < self.caps
        assert self.caps < self.any
        assert self.empty <= self.empty
        assert self.caps <= self.caps
        assert self.caps <= self.any
        assert self.empty == "EMPTY"
        assert self.caps != self.any
        assert self.empty != self.any
        assert self.any > self.empty
        assert self.any >= self.empty

    def testFilters(self):
        name = 'video/x-raw-yuv'
        filtercaps = gst.Caps(*[struct for struct in self.caps if struct.get_name() == name])
        intersection = self.caps & 'video/x-raw-yuv'
        assert filtercaps == intersection

    def doSubtract(self, set, subset):
        '''mimic the test in GStreamer core's testsuite/caps/subtract.c'''
        assert not set - set
        assert not subset - subset
        assert not subset - set
        test = set - subset
        assert test
        test2 = test | subset
        test = test2 - set
        assert not test
        #our own extensions foolow here
        assert subset == set & subset
        assert set == set | subset
        assert set - subset == set ^ subset

    def testSubtract(self):
        self.doSubtract(
            gst.Caps ("some/mime, _int = [ 1, 2 ], list = { \"A\", \"B\", \"C\" }"),
            gst.Caps ("some/mime, _int = 1, list = \"A\""))
        self.doSubtract(
            gst.Caps ("some/mime, _double = (double) 1.0; other/mime, _int = { 1, 2 }"),
            gst.Caps ("some/mime, _double = (double) 1.0"))

    def testNoneValue(self):
        caps = gst.Caps("foo")
        
        def invalid_assignment():
            caps[0]["bar"] = None
        self.assertRaises(TypeError, invalid_assignment)

        def invalid_set_value():
            caps[0].set_value("bar", None)
        self.assertRaises(TypeError, invalid_set_value)


if __name__ == "__main__":
    unittest.main()
