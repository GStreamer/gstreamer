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

    def _testCapsStructureChange(self):
	'test if changing the structure of the caps works by reference'
	assert self.structure['width'] == 10
        self.structure['width'] = 5
	assert self.structure['width'] == 5.0
	# check if we changed the caps as well
	structure = self.caps[0]
	assert structure['width'] == 5.0

if __name__ == "__main__":
    unittest.main()
