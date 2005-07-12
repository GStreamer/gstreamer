#!/usr/bin/python
#
# testsuite for gstreamer.Element

from common import gst, unittest

class ElementTest(unittest.TestCase):
    name = 'fakesink'
    alias = 'sink'
    
    def testGoodConstructor(self):
        element = gst.element_factory_make(self.name, self.alias)
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
        self.element = gst.element_factory_make('fakesink', 'sink')

    def checkError(self, old_state, state, name):
        assert self.element.get_state() == gst.STATE_NULL
        assert self.element.set_state(old_state)
        assert self.element.get_state() == old_state
        self.element.set_property('state-error', name)
        self.error = False
        def error_cb(element, source, gerror, debug):
            assert isinstance(element, gst.Element)
            assert element == self.element
            assert isinstance(source, gst.Element)
            assert source == self.element
            assert isinstance(gerror, gst.GError)
            self.error = True
            
        self.element.connect('error', error_cb)
        self.element.set_state (state)
        assert self.error, 'error not set'
        #assert error_message.find('ERROR') != -1
        
        self.element.get_state() == old_state, 'state changed'
        
##     def testStateErrorNullReady(self):
##         self.checkError(gst.STATE_NULL, gst.STATE_READY,
##                         self.FAKESINK_STATE_ERROR_NULL_READY)
        
##     def testStateErrorReadyPaused(self):
##         self.checkError(gst.STATE_READY, gst.STATE_PAUSED,
##                         self.FAKESINK_STATE_ERROR_READY_PAUSED)
        
##     def testStateErrorPausedPlaying(self):
##         self.checkError(gst.STATE_PAUSED, gst.STATE_PLAYING,
##                         self.FAKESINK_STATE_ERROR_PAUSED_PLAYING)        

##     def testStateErrorPlayingPaused(self):
##         self.checkError(gst.STATE_PLAYING, gst.STATE_PAUSED,
##                         self.FAKESINK_STATE_ERROR_PLAYING_PAUSED)
        
##     def testStateErrorPausedReady(self):
##         self.checkError(gst.STATE_PAUSED, gst.STATE_READY,
##                         self.FAKESINK_STATE_ERROR_PAUSED_READY)

##     def testStateErrorReadyNull(self):
##         self.checkError(gst.STATE_READY, gst.STATE_NULL,
##                         self.FAKESINK_STATE_ERROR_READY_NULL)

    def checkStateChange(self, old, new):
        def state_change_cb(element, old_s, new_s):
            assert isinstance(element, gst.Element)
            assert element == self.element
            assert old_s == old
            assert new_s == new
            
        assert self.element.set_state(old)
        assert self.element.get_state() == old

        self.element.connect('state-change', state_change_cb)

        assert self.element.set_state(new)
        assert self.element.get_state() == new
        
    def testStateChangeNullReady(self):
        self.checkStateChange(gst.STATE_NULL, gst.STATE_READY)
        
##     def testStateChangeReadyPaused(self):
##         self.checkStateChange(gst.STATE_READY, gst.STATE_PAUSED)

##     def testStateChangePausedPlaying(self):
##         self.checkStateChange(gst.STATE_PAUSED, gst.STATE_PLAYING)
        
##     def testStateChangePlayingPaused(self):
##         self.checkStateChange(gst.STATE_PLAYING, gst.STATE_PAUSED)
        
##     def testStateChangePausedReady(self):
##         self.checkStateChange(gst.STATE_PAUSED, gst.STATE_READY)

    def testStateChangeReadyNull(self):
        self.checkStateChange(gst.STATE_READY, gst.STATE_NULL)

class NonExistentTest(ElementTest):
    name = 'this-element-does-not-exist'
    alias = 'no-alias'
    
    testGoodConstructor = lambda s: None
    testGoodConstructor2 = lambda s: None

class FileSrcTest(ElementTest):
    name = 'filesrc'
    alias = 'source'
    
class FileSinkTest(ElementTest):
    name = 'filesink'
    alias = 'sink'

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
        assert get_name(-1) == 'UNKNOWN!(-1)'
        self.assertRaises(TypeError, get_name, '')
        
class QueryTest(unittest.TestCase):
    def setUp(self):
        self.pipeline = gst.parse_launch('fakesrc name=source ! fakesink')
        self.element = self.pipeline.get_by_name('source')
        
    def testQuery(self):
        res = self.element.query_position(gst.FORMAT_BYTES)
        assert res
        assert res[0] == 0
        assert res[1] == -1
        res = self.element.query_position(gst.FORMAT_TIME)
        assert not res

class QueueTest(unittest.TestCase):
    def testConstruct(self):
        queue = gst.element_factory_make('queue')
        assert isinstance(queue, gst.Queue)
        assert queue.get_name() == 'queue0'

if __name__ == "__main__":
    unittest.main()
