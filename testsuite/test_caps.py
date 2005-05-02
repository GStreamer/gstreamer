import sys
from common import gst, unittest

class CapsTest(unittest.TestCase):
    def setUp(self):
	self.caps = gst.caps_from_string('video/x-raw-yuv,width=10,framerate=5.0;video/x-raw-rgb,width=15,framerate=10.0')
	self.structure = self.caps[0]

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
        struct = gst.structure_from_string('video/x-raw-yuv,width=10')
        caps = gst.Caps(struct)
	assert isinstance(caps, gst.Caps)
        assert len(caps) == 1
	assert isinstance(caps[0], gst.Structure)
        assert caps[0].get_name() == 'video/x-raw-yuv'
	assert isinstance(caps[0]['width'], int)
        assert caps[0]['width'] == 10

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

    def testCapsRefernceStructs(self):
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
	#assert gst.Caps('ANY') # not empty even though it has no structures
	assert not gst.Caps() # empty
	assert not gst.Caps('EMPTY') # also empty
	assert gst.Caps('your/mom')
        
if __name__ == "__main__":
    unittest.main()
