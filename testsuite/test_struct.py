import sys
from common import gst, unittest

class StructureTest(unittest.TestCase):
    def setUp(self):
        self.struct = gst.structure_from_string('video/x-raw-yuv,width=10,foo="bar",pixel-aspect-ratio=1/2,framerate=5.0')

    #def foo(self):
    #    gst.structure_from_string("foo")
        
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
        
    def testCreateFourCC(self):
	self.struct['fourcc'] = "(fourcc)XVID"
        #assert self.struct.has_key('fourcc')
        #print self.struct.to_string()
        #assert isinstance(self.struct['fourcc'], int)
	#assert self.struct['integer'] == 5, self.struct['integer']
        
    def testStructureChange(self):
        #assert structure['pixel-aspect-ratio'].numerator == 1
        #assert structure['pixel-aspect-ratio'].denominator == 2
        #assert float(structure['pixel-aspect-ratio']) == 0.5
	#structure['pixel-aspect-ratio'] = gst.Fraction(3, 4)
        #assert structure['pixel-aspect-ratio'].numerator == 3
        #assert structure['pixel-aspect-ratio'].denominator == 4
        #assert float(structure['pixel-aspect-ratio']) == 0.75
        
	assert self.struct['framerate'] == 5.0
	self.struct['framerate'] = 10.0
	assert self.struct['framerate'] == 10.0

	# a list of heights
	#structure['height'] = (20, 40, 60)
	#assert structure['width'] == (20, 40, 60)
	# FIXME: add ranges

if __name__ == "__main__":
    unittest.main()
