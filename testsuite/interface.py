from common import gst, unittest

import gobject
from gst import interfaces

class Availability(unittest.TestCase):
    def testXOverlay(self):
        assert hasattr(interfaces, 'XOverlay')
        assert issubclass(interfaces.XOverlay, gobject.GInterface)

    def testMixer(self):
        assert hasattr(interfaces, 'Mixer')
        assert issubclass(interfaces.Mixer, gobject.GInterface)

class FunctionCall(unittest.TestCase):
    def testXOverlay(self):
        element = gst.element_factory_make('xvimagesink')
        assert isinstance(element, gst.Element)
        assert isinstance(element, interfaces.XOverlay)
        element.set_xwindow_id(0L)
        
if __name__ == "__main__":
    unittest.main()
