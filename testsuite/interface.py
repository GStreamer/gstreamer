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

if getattr(gobject, 'pygtk_version', ()) >= (2,3,92):
    class FunctionCall(unittest.TestCase):
        def testXOverlay(self):
            element = gst.Element('xvimagesink')
            assert isinstance(element, gst.Element)
            assert isinstance(element, interfaces.XOverlay)
            element.set_xwindow_id(0L)
        
if __name__ == "__main__":
    unittest.main()
