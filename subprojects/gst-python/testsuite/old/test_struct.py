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

class StructureTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.struct = gst.structure_from_string('video/x-raw-yuv,width=10,foo="bar",pixel-aspect-ratio=1/2,framerate=5/1,boolean=(boolean)true')

    def testName(self):
        assert self.struct.get_name() == 'video/x-raw-yuv'
        self.struct.set_name('foobar')
        assert self.struct.get_name() == 'foobar'
        
    def testInt(self):
        assert self.struct.has_key('width')
        assert isinstance(self.struct['width'], int)
        assert self.struct['width'] == 10, self.struct['width']
        self.struct['width'] = 5
        assert self.struct.has_key('width')
        assert isinstance(self.struct['width'], int)
        assert self.struct['width'] == 5, self.struct['width']

    def testString(self):
        assert self.struct.has_key('foo')
        assert isinstance(self.struct['foo'], unicode)
        assert self.struct['foo'] == 'bar', self.struct['foo']
        self.struct['foo'] = 'baz'
        assert self.struct.has_key('foo')
        assert isinstance(self.struct['foo'], unicode)
        assert self.struct['foo'] == 'baz', self.struct['foo']

    def testBoolean(self):
        assert self.struct.has_key('boolean')
        assert isinstance(self.struct['boolean'], bool)
        assert self.struct['boolean'] == True, self.struct['boolean']
        self.struct['boolean'] = False
        assert self.struct.has_key('boolean')
        assert isinstance(self.struct['boolean'], bool)
        assert self.struct['boolean'] == False, self.struct['boolean']

    def testCreateInt(self):
        self.struct['integer'] = 5
        assert self.struct.has_key('integer')
        assert isinstance(self.struct['integer'], int)
        assert self.struct['integer'] == 5, self.struct['integer']
        
    def testGstValue(self):
        s = self.struct
        s['fourcc'] = gst.Fourcc('XVID')
        assert s['fourcc'].fourcc == 'XVID'
        s['frac'] = gst.Fraction(3,4)
        assert s['frac'].num == 3
        assert s['frac'].denom == 4
        s['fracrange'] = gst.FractionRange(gst.Fraction(0,1),
                                           gst.Fraction(25,3))
        assert s['fracrange'].low.num == 0
        assert s['fracrange'].low.denom == 1
        assert s['fracrange'].high.num == 25
        assert s['fracrange'].high.denom == 3
        s['intrange'] = gst.IntRange(5,21)
        assert s['intrange'].low == 5
        assert s['intrange'].high == 21
        s['doublerange'] = gst.DoubleRange(6.,21.)
        assert s['doublerange'].low == 6.
        assert s['doublerange'].high == 21.
        s['fixedlist'] = (4, 5, 6)
        assert isinstance(s['fixedlist'], tuple)
        assert s['fixedlist'] == (4, 5, 6)
        s['list'] = [4, 5, 6]
        assert isinstance(s['list'], list)
        assert s['list'] == [4, 5, 6]
        s['boolean'] = True
        assert isinstance(s['boolean'], bool)
        assert s['boolean'] == True
        
        # finally, some recursive tests
        s['rflist'] = ([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g'])
        assert s['rflist'] == ([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g'])
        s['rlist'] = [([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g']), 'h']
        assert s['rlist'] == [([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g']), 'h']

    def testStructureChange(self):
        assert self.struct['framerate'] == gst.Fraction(5, 1)
        self.struct['framerate'] = gst.Fraction(10, 1)
        assert self.struct['framerate'] == gst.Fraction(10, 1)
        self.struct['pixel-aspect-ratio'] = gst.Fraction(4, 2)
        assert self.struct['pixel-aspect-ratio'].num == 2
        assert self.struct['pixel-aspect-ratio'].denom == 1

    def testKeys(self):
        k = self.struct.keys()
        self.failUnless(k)
        self.assertEquals(len(k), 5)
        self.failUnless("width" in k)
        self.failUnless("foo" in k)
        self.failUnless("framerate" in k)
        self.failUnless("pixel-aspect-ratio" in k)
        self.failUnless("boolean" in k)
 
if __name__ == "__main__":
    unittest.main()
