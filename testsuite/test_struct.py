import sys
from common import gst, unittest

class StructureTest(unittest.TestCase):
    def testStructureChange(self):
	caps = gst.caps_from_string('video/x-raw-yuv,width=10,pixel-aspect-ratio=1/2,framerate=5.0')
	structure = caps.get_structure(0)
	assert structure['width'] == 10
	structure['width'] = 5
	assert structure['width'] == 5, structure['width']

        #assert structure['pixel-aspect-ratio'].numerator == 1
        #assert structure['pixel-aspect-ratio'].denominator == 2
        #assert float(structure['pixel-aspect-ratio']) == 0.5
	#structure['pixel-aspect-ratio'] = gst.Fraction(3, 4)
        #assert structure['pixel-aspect-ratio'].numerator == 3
        #assert structure['pixel-aspect-ratio'].denominator == 4
        #assert float(structure['pixel-aspect-ratio']) == 0.75
        
	assert structure['framerate'] == 5.0
	structure['framerate'] = 10.0
	assert structure['framerate'] == 10.0

	# a list of heights
	#structure['height'] = (20, 40, 60)
	#assert structure['width'] == (20, 40, 60)
	# FIXME: add ranges

if __name__ == "__main__":
    unittest.main()
