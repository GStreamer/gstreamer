import sys
from common import gst, unittest

class CapsTest(unittest.TestCase):
    def testCapsMime(self):
	caps = gst.caps_from_string('video/x-raw-yuv,width=10,framerate=5.0')
	structure = caps.get_structure(0)
	mime = structure.get_name()
	assert mime == 'video/x-raw-yuv'

if __name__ == "__main__":
    unittest.main()
