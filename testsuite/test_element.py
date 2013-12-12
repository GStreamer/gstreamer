# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2002 David I. Lehn
# Copyright (C) 2004 Johan Dahlin
# Copyright (C) 2005 Edward Hervey
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA

from common import gst, unittest, TestCase, pygobject_2_13

import sys

# since I can't subclass gst.Element for some reason, I use a bin here
# it don't matter to Jesus
class TestElement(gst.Bin):
    def break_it_down(self):
        self.debug('Hammer Time')

class ElementTest(TestCase):
    name = 'fakesink'
    alias = 'sink'

    def testGoodConstructor(self):
        element = gst.element_factory_make(self.name, self.alias)
        assert element is not None, 'element is None'
        assert isinstance(element, gst.Element)
        assert element.get_name() == self.alias

## FIXME : Make a new test for state changes, using bus signals
        
## class FakeSinkTest(ElementTest):
##     FAKESINK_STATE_ERROR_NONE           = "0"
##     FAKESINK_STATE_ERROR_NULL_READY,    = "1"
##     FAKESINK_STATE_ERROR_READY_PAUSED,  = "2"
##     FAKESINK_STATE_ERROR_PAUSED_PLAYING = "3"
##     FAKESINK_STATE_ERROR_PLAYING_PAUSED = "4"
##     FAKESINK_STATE_ERROR_PAUSED_READY   = "5"
##     FAKESINK_STATE_ERROR_READY_NULL     = "6"

##     name = 'fakesink'
##     alias = 'sink'
##     def setUp(self):
##         ElementTest.setUp(self)
##         self.element = gst.element_factory_make('fakesink', 'sink')

##     def tearDown(self):
##         self.element.set_state(gst.STATE_NULL)
##         del self.element
##         ElementTest.tearDown(self)

##     def checkError(self, old_state, state, name):
##         assert self.element.get_state() == gst.STATE_NULL
##         assert self.element.set_state(old_state)
##         assert self.element.get_state() == old_state
##         self.element.set_property('state-error', name)
##         self.error = False
##         def error_cb(element, source, gerror, debug):
##             assert isinstance(element, gst.Element)
##             assert element == self.element
##             assert isinstance(source, gst.Element)
##             assert source == self.element
##             assert isinstance(gerror, gst.GError)
##             self.error = True
            
##         self.element.connect('error', error_cb)
##         self.element.set_state (state)
##         assert self.error, 'error not set'
##         #assert error_message.find('ERROR') != -1
        
##         self.element.get_state() == old_state, 'state changed'
        
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

##     def checkStateChange(self, old, new):
##         def state_change_cb(element, old_s, new_s):
##             assert isinstance(element, gst.Element)
##             assert element == self.element
##             assert old_s == old
##             assert new_s == new
            
##         assert self.element.set_state(old)
##         assert self.element.get_state(0.0)[1] == old

## # FIXME: replace with messages
## #        self.element.connect('state-change', state_change_cb)

##         assert self.element.set_state(new)
##         assert self.element.get_state(0.0)[1] == new
        
##     def testStateChangeNullReady(self):
##         self.checkStateChange(gst.STATE_NULL, gst.STATE_READY)
        
##     def testStateChangeReadyPaused(self):
##         self.checkStateChange(gst.STATE_READY, gst.STATE_PAUSED)

##     def testStateChangePausedPlaying(self):
##         self.checkStateChange(gst.STATE_PAUSED, gst.STATE_PLAYING)
        
##     def testStateChangePlayingPaused(self):
##         self.checkStateChange(gst.STATE_PLAYING, gst.STATE_PAUSED)
        
##     def testStateChangePausedReady(self):
##         self.checkStateChange(gst.STATE_PAUSED, gst.STATE_READY)

##     def testStateChangeReadyNull(self):
##         self.checkStateChange(gst.STATE_READY, gst.STATE_NULL)

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

class ElementName(TestCase):
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
            
        assert get_name(gst.STATE_VOID_PENDING) == 'VOID_PENDING'
        assert get_name(-1) == 'UNKNOWN!(-1)'
        self.assertRaises(TypeError, get_name, '')
        
class QueryTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.pipeline = gst.parse_launch('fakesrc name=source ! fakesink')
        self.assertEquals(self.pipeline.__gstrefcount__, 1)

        self.element = self.pipeline.get_by_name('source')
        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(self.element.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.element), pygobject_2_13 and 2 or 3)

    def tearDown(self):
        del self.pipeline
        del self.element
        TestCase.tearDown(self)
        
    def testQuery(self):
        gst.debug('querying fakesrc in FORMAT_BYTES')
        res = self.element.query_position(gst.FORMAT_BYTES)
        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.pipeline), pygobject_2_13 and 2 or 3)
        self.assertEquals(self.element.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.element), pygobject_2_13 and 2 or 3)
        assert res
        assert res[0] == 0
        self.assertRaises(gst.QueryError, self.element.query_position,
            gst.FORMAT_TIME)
        self.gccollect()

class QueueTest(TestCase):
    def testConstruct(self):
        queue = gst.element_factory_make('queue')
        assert queue.get_name() == 'queue0'
        self.assertEquals(queue.__gstrefcount__, 1)

class DebugTest(TestCase):
    def testDebug(self):
        e = gst.element_factory_make('fakesrc')
        e.error('I am an error string')
        e.warning('I am a warning string')
        e.info('I am an info string')
        e.debug('I am a debug string')
        e.log('I am a log string')
        e.debug('I am a formatted %s %s' % ('log', 'string'))

    def testElementDebug(self):
        e = TestElement("testelement")
        e.set_property("name", "testelement")
        e.break_it_down()
        
class LinkTest(TestCase):
    def testLinkNoPads(self):
        src = gst.Bin()
        sink = gst.Bin()
        self.assertRaises(gst.LinkError, src.link, sink)

    def testLink(self):
        src = gst.element_factory_make('fakesrc')
        sink = gst.element_factory_make('fakesink')
        self.failUnless(src.link(sink))
        # FIXME: this unlink leaks, no idea why
        # src.unlink(sink)
        # print src.__gstrefcount__

    def testLinkPads(self):
        src = gst.element_factory_make('fakesrc')
        sink = gst.element_factory_make('fakesink')
        # print src.__gstrefcount__
        self.failUnless(src.link_pads("src", sink, "sink"))
        src.unlink_pads("src", sink, "sink")

    def testLinkFiltered(self):
        # a filtered link uses capsfilter and thus needs a bin
        bin = gst.Bin()
        src = gst.element_factory_make('fakesrc')
        sink = gst.element_factory_make('fakesink')
        bin.add(src, sink)
        caps = gst.caps_from_string("audio/x-raw-int")

        self.failUnless(src.link(sink, caps))

        # DANGER WILL.  src is not actually connected to sink, since
        # there's a capsfilter in the way.  What a leaky abstraction.
        # FIXME
        # src.unlink(sink)

        # instead, mess with pads directly
        pad = src.get_pad('src')
        pad.unlink(pad.get_peer())
        pad = sink.get_pad('sink')
        pad.get_peer().unlink(pad)

if __name__ == "__main__":
    unittest.main()
