from common import gst, unittest

import gobject

class Availability(unittest.TestCase):
    def testXOverlay(self):
        assert hasattr(gst.interfaces, 'XOverlay')
        assert issubclass(gst.interfaces.XOverlay, gobject.GInterface)

    def testMixer(self):
        assert hasattr(gst.interfaces, 'Mixer')
        assert issubclass(gst.interfaces.Mixer, gobject.GInterface)

class FunctionCall(unittest.TestCase):
    def testXOverlay(self):
        element = gst.element_factory_make('xvimagesink')
        assert isinstance(element, gst.Element)
        assert isinstance(element, gst.interfaces.XOverlay)
        element.set_xwindow_id(0L)
        
if __name__ == "__main__":
    unittest.main()
