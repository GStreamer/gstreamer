#!/usr/bin/python
#
# testsuite for gstreamer.Element

import common
from common import gst, unittest

class ElementTest(unittest.TestCase):
    name = 'fakesink'
    alias = 'sink'
    
    def testBadConstruct(self):
        self.assertRaises(TypeError, gst.Element)
        self.assertRaises(TypeError, gst.Element, None)

    def testGoodConstructor(self):
        element = gst.Element(self.name, self.alias)
        assert element is not None, 'element is None'
        assert isinstance(element, gst.Element)
        assert element.get_name() == self.alias
        
class FakeSinkTest(ElementTest):
    FAKESINK_STATE_ERROR_NONE           = "0"
    FAKESINK_STATE_ERROR_NULL_READY,    = "1"
    FAKESINK_STATE_ERROR_READY_PAUSED,  = "2"
    FAKESINK_STATE_ERROR_PAUSED_PLAYING = "3"
    FAKESINK_STATE_ERROR_PLAYING_PAUSED = "4"
    FAKESINK_STATE_ERROR_PAUSED_READY   = "5"
    FAKESINK_STATE_ERROR_READY_NULL     = "6"

    name = 'fakesink'
    alias = 'sink'
    def setUp(self):
        self.element = gst.Element('fakesink', 'sink')

    def testStateError(self):
        self.element.set_property('state-error',
                                  self.FAKESINK_STATE_ERROR_NULL_READY)
        def error_cb(element, source, error, debug):
            assert isinstance(element, gst.Element)
            assert element == self.element
            assert isinstance(source, gst.Element)
            assert source == self.element
            assert isinstance(error, gst.GError)
        
        self.element.connect('error', error_cb)
        common.disable_stderr()
        self.element.set_state(gst.STATE_READY)
        common.enable_stderr()
        
class NonExistentTest(ElementTest):
    name = 'this-element-does-not-exist'
    alias = 'no-alias'
    
    def testGoodConstructor(self):
        pass
    
class ElementName(unittest.TestCase):
    def testElementStateGetName(self):
        get_name = gst.element_state_get_name
        for state in ('NULL',
                      'READY',
                      'PLAYING',
                      'PAUSED'):
            name = 'STATE_' + state
            assert hasattr(gst, name)
            attr = getattr(gst, name)
            assert get_name(attr) == state
            
        assert get_name(gst.STATE_VOID_PENDING) == 'NONE_PENDING'
        assert get_name(-1) == 'UNKNOWN!'
        self.assertRaises(TypeError, get_name, '')
        
if __name__ == "__main__":
    unittest.main()
