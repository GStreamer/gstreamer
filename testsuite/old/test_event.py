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

import os
import sys
import time
import tempfile

from common import gst, unittest, testhelper, TestCase

class EventTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.pipeline = gst.parse_launch('fakesrc ! fakesink name=sink')
        self.sink = self.pipeline.get_by_name('sink')
        self.pipeline.set_state(gst.STATE_PLAYING)

    def tearDown(self):
        gst.debug('setting pipeline to NULL')
        self.pipeline.set_state(gst.STATE_NULL)
        gst.debug('set pipeline to NULL')
        # FIXME: wait for state change thread to die
        while self.pipeline.__gstrefcount__ > 1:
            gst.debug('waiting for self.pipeline G rc to drop to 1')
            time.sleep(0.1)
        self.assertEquals(self.pipeline.__gstrefcount__, 1)

        del self.sink
        del self.pipeline
        TestCase.tearDown(self)
        
    def testEventSeek(self):
        # this event only serves to change the rate of data transfer
        event = gst.event_new_seek(1.0, gst.FORMAT_BYTES, gst.SEEK_FLAG_FLUSH,
            gst.SEEK_TYPE_NONE, 0, gst.SEEK_TYPE_NONE, 0)
        # FIXME: but basesrc goes into an mmap/munmap spree, needs to be fixed

        event = gst.event_new_seek(1.0, gst.FORMAT_BYTES, gst.SEEK_FLAG_FLUSH,
            gst.SEEK_TYPE_SET, 0, gst.SEEK_TYPE_NONE, 0)
        assert event
        gst.debug('sending event')
        self.sink.send_event(event)
        gst.debug('sent event')

        self.assertEqual(event.parse_seek(), (1.0, gst.FORMAT_BYTES, gst.SEEK_FLAG_FLUSH,
            gst.SEEK_TYPE_SET, 0, gst.SEEK_TYPE_NONE, 0))

    def testWrongEvent(self):
        buffer = gst.Buffer()
        self.assertRaises(TypeError, self.sink.send_event, buffer)
        number = 1
        self.assertRaises(TypeError, self.sink.send_event, number)


class EventFileSrcTest(TestCase):

   def setUp(self):
       TestCase.setUp(self)
       gst.info("start")
       self.filename = tempfile.mktemp()
       open(self.filename, 'w').write(''.join(map(str, range(10))))
       
       self.pipeline = gst.parse_launch('filesrc name=source location=%s blocksize=1 ! fakesink signal-handoffs=1 name=sink' % self.filename)
       self.source = self.pipeline.get_by_name('source')
       self.sink = self.pipeline.get_by_name('sink')
       self.sigid = self.sink.connect('handoff', self.handoff_cb)
       self.bus = self.pipeline.get_bus()
       
   def tearDown(self):
       self.pipeline.set_state(gst.STATE_NULL)
       self.sink.disconnect(self.sigid)
       if os.path.exists(self.filename):
           os.remove(self.filename)
       del self.bus
       del self.pipeline
       del self.source
       del self.sink
       del self.handoffs
       TestCase.tearDown(self)

   def handoff_cb(self, element, buffer, pad):
       self.handoffs.append(str(buffer))

   def playAndIter(self):
       self.handoffs = []
       self.pipeline.set_state(gst.STATE_PLAYING)
       assert self.pipeline.set_state(gst.STATE_PLAYING)
       while 42:
           msg = self.bus.pop()
           if msg and msg.type == gst.MESSAGE_EOS:
               break
       assert self.pipeline.set_state(gst.STATE_PAUSED)
       handoffs = self.handoffs
       self.handoffs = []
       return handoffs

   def sink_seek(self, offset, method=gst.SEEK_TYPE_SET):
       self.sink.seek(1.0, gst.FORMAT_BYTES, gst.SEEK_FLAG_FLUSH,
                      method, offset,
                      gst.SEEK_TYPE_NONE, 0)
      
   def testSimple(self):
       handoffs = self.playAndIter()
       assert handoffs == map(str, range(10))
   
   def testSeekCur(self):
       self.sink_seek(8)
       self.playAndIter()

class TestEmit(TestCase):
    def testEmit(self):
        object = testhelper.get_object()
        object.connect('event', self._event_cb)
        
        # First emit from C
        testhelper.emit_event(object)

        # Then emit from Python
        object.emit('event', gst.event_new_eos())

    def _event_cb(self, obj, event):
        assert isinstance(event, gst.Event)
    

class TestDelayedEventProbe(TestCase):
    # this test:
    # starts a pipeline with only a source
    # adds an event probe to catch the (first) new-segment
    # adds a buffer probe to "autoplug" and send out this event
    def setUp(self):
        TestCase.setUp(self)
        self.pipeline = gst.Pipeline()
        self.src = gst.element_factory_make('fakesrc')
        self.src.set_property('num-buffers', 10)
        self.pipeline.add(self.src)
        self.srcpad = self.src.get_pad('src')
        
    def tearDown(self):
        gst.debug('setting pipeline to NULL')
        self.pipeline.set_state(gst.STATE_NULL)
        gst.debug('set pipeline to NULL')
        # FIXME: wait for state change thread to die
        while self.pipeline.__gstrefcount__ > 1:
            gst.debug('waiting for self.pipeline G rc to drop to 1')
            time.sleep(0.1)
        self.assertEquals(self.pipeline.__gstrefcount__, 1)

    def testProbe(self):
        self.srcpad.add_event_probe(self._event_probe_cb)
        self._buffer_probe_id = self.srcpad.add_buffer_probe(
            self._buffer_probe_cb)

        self._newsegment = None
        self._eos = None
        self._had_buffer = False

        self.pipeline.set_state(gst.STATE_PLAYING)

        while not self._eos:
            time.sleep(0.1)

        # verify if our newsegment event is still around and valid
        self.failUnless(self._newsegment)
        self.assertEquals(self._newsegment.type, gst.EVENT_NEWSEGMENT)
        self.assertEquals(self._newsegment.__grefcount__, 1)

        # verify if our eos event is still around and valid
        self.failUnless(self._eos)
        self.assertEquals(self._eos.type, gst.EVENT_EOS)
        self.assertEquals(self._eos.__grefcount__, 1)
 
    def _event_probe_cb(self, pad, event):
        if event.type == gst.EVENT_NEWSEGMENT:
            self._newsegment = event
            self.assertEquals(event.__grefcount__, 3)
            # drop the event, we're storing it for later sending
            return False

        if  event.type == gst.EVENT_EOS:
            self._eos = event
            # we also want fakesink to get it
            return True

        # sinks now send Latency events upstream
        if event.type == gst.EVENT_LATENCY:
            return True

        self.fail("Got an unknown event %r" % event)

    def _buffer_probe_cb(self, pad, buffer):
        self.failUnless(self._newsegment)

        # fake autoplugging by now putting in a fakesink
        sink = gst.element_factory_make('fakesink')
        self.pipeline.add(sink)
        self.src.link(sink)
        sink.set_state(gst.STATE_PLAYING)

        pad = sink.get_pad('sink')
        pad.send_event(self._newsegment)

        # we don't want to be called again
        self.srcpad.remove_buffer_probe(self._buffer_probe_id)
        
        self._had_buffer = True
        # now let the buffer through
        return True

class TestEventCreationParsing(TestCase):

    def testEventStep(self):
        if hasattr(gst.Event, "parse_step"):
            e = gst.event_new_step(gst.FORMAT_TIME, 42, 1.0, True, True)

            self.assertEquals(e.type, gst.EVENT_STEP)

            fmt, amount, rate, flush, intermediate = e.parse_step()
            self.assertEquals(fmt, gst.FORMAT_TIME)
            self.assertEquals(amount, 42)
            self.assertEquals(rate, 1.0)
            self.assertEquals(flush, True)
            self.assertEquals(intermediate, True)

if __name__ == "__main__":
    unittest.main()
