import sys
from common import gst, unittest

class CapsTest(unittest.TestCase):
    def setUp(self):
	self.caps = gst.caps_from_string('video/x-raw-yuv,width=10,framerate=5.0;video/x-raw-rgb,width=15,framerate=10.0')
	self.structure = self.caps.get_structure(0)

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

    def testCapsConstructFromStructures(self):
        struct1 = gst.structure_from_string('video/x-raw-yuv,width=10')
        struct2 = gst.structure_from_string('video/x-raw-rgb,width=20')
        caps = gst.Caps(struct1, struct2)
	assert isinstance(caps, gst.Caps)
        assert len(caps) == 2
	assert isinstance(caps[0], gst.Structure)
	assert isinstance(caps[1], gst.Structure)
        assert caps[0].get_name() == 'video/x-raw-yuv'
        assert caps[1].get_name() == 'video/x-raw-rgb'
	assert isinstance(caps[0]['width'], int)
	assert isinstance(caps[1]['width'], int)
        assert caps[0]['width'] == 10
        assert caps[1]['width'] == 20

    def testCapsStructureChange(self):
	'test if changing the structure of the caps works by reference'
	assert self.structure['width'] == 10
        self.structure['width'] = 5
	assert self.structure['width'] == 5.0
	# check if we changed the caps as well
	structure = self.caps[0]
	assert structure['width'] == 5.0

if __name__ == "__main__":
    unittest.main()
