from common import gst, unittest

try:
    from gst import interfaces
except:
    raise SystemExit

import gobject

class Availability(unittest.TestCase):
    def testXOverlay(self):
        assert hasattr(interfaces, 'XOverlay')
        assert issubclass(interfaces.XOverlay, gobject.GInterface)

    def testMixer(self):
        assert hasattr(interfaces, 'Mixer')
        assert issubclass(interfaces.Mixer, gobject.GInterface)

if __name__ == "__main__":
    unittest.main()
