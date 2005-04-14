import sys
from common import gst, unittest

class StructureTest(unittest.TestCase):
    def setUp(self):
        self.struct = gst.structure_from_string('video/x-raw-yuv,width=10,foo="bar",pixel-aspect-ratio=1/2,framerate=5.0')

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
        assert isinstance(self.struct['foo'], str)
	assert self.struct['foo'] == 'bar', self.struct['foo']
	self.struct['foo'] = 'baz'
        assert self.struct.has_key('foo')
        assert isinstance(self.struct['foo'], str)
	assert self.struct['foo'] == 'baz', self.struct['foo']

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
        
        # finally, some recursive tests
        s['rflist'] = ([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g'])
        assert s['rflist'] == ([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g'])
        s['rlist'] = [([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g']), 'h']
        assert s['rlist'] == [([(['a', 'b'], ['c', 'd']),'e'], ['f', 'g']), 'h']

    def testStructureChange(self):
	assert self.struct['framerate'] == 5.0
	self.struct['framerate'] = 10.0
	assert self.struct['framerate'] == 10.0
        self.struct['pixel-aspect-ratio'] = gst.Fraction(4, 2)
        assert self.struct['pixel-aspect-ratio'].num == 2
        assert self.struct['pixel-aspect-ratio'].denom == 1

if __name__ == "__main__":
    unittest.main()
