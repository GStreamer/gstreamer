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
    def FIXME_testXOverlay(self):
        # obviously a testsuite is not allowed to instantiate this
        # since it needs a running X or will fail.  find some way to
        # deal with that.
        element = gst.element_factory_make('xvimagesink')
        assert isinstance(element, gst.Element)
        assert isinstance(element, gst.interfaces.XOverlay)
        element.set_xwindow_id(0L)
        
if __name__ == "__main__":
    unittest.main()
