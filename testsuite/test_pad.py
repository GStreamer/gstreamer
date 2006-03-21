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
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA

from common import gst, unittest, TestCase

import sys
import time

class PadTemplateTest(TestCase):
    def testConstructor(self):
        template = gst.PadTemplate("template", gst.PAD_SINK,
            gst.PAD_ALWAYS, gst.caps_from_string("audio/x-raw-int"))
        self.failUnless(template)
        self.assertEquals(sys.getrefcount(template), 3)
        #self.assertEquals(template.__gstrefcount__, 1)

class PadPushUnlinkedTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.src = gst.Pad("src", gst.PAD_SRC)
        self.sink = gst.Pad("sink", gst.PAD_SINK)

    def tearDown(self):
        self.assertEquals(sys.getrefcount(self.src), 3)
        self.assertEquals(self.src.__gstrefcount__, 1)
        del self.src
        self.assertEquals(sys.getrefcount(self.sink), 3)
        self.assertEquals(self.sink.__gstrefcount__, 1)
        del self.sink
        TestCase.tearDown(self)

    def testNoProbe(self):
        self.buffer = gst.Buffer()
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.assertEquals(self.src.push(self.buffer), gst.FLOW_NOT_LINKED)
        # pushing it takes a ref in the python wrapper to keep buffer
        # alive afterwards; but the core unrefs the ref it receives
        self.assertEquals(self.buffer.__grefcount__, 1)

    def testFalseProbe(self):
        id = self.src.add_buffer_probe(self._probe_handler, False)
        self.buffer = gst.Buffer()
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.assertEquals(self.src.push(self.buffer), gst.FLOW_OK)
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.src.remove_buffer_probe(id)

    def testTrueProbe(self):
        id = self.src.add_buffer_probe(self._probe_handler, True)
        self.buffer = gst.Buffer()
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.assertEquals(self.src.push(self.buffer), gst.FLOW_NOT_LINKED)
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.src.remove_buffer_probe(id)

    def _probe_handler(self, pad, buffer, ret):
        return ret

class PadPushLinkedTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.src = gst.Pad("src", gst.PAD_SRC)
        self.sink = gst.Pad("sink", gst.PAD_SINK)
        caps = gst.caps_from_string("foo/bar")
        self.src.set_caps(caps)
        self.sink.set_caps(caps)
        self.sink.set_chain_function(self._chain_func)
        self.src.link(self.sink)
        self.buffers = []

    def tearDown(self):
        self.assertEquals(sys.getrefcount(self.src), 3)
        self.assertEquals(self.src.__gstrefcount__, 1)
        del self.src
        self.assertEquals(sys.getrefcount(self.sink), 3)
        self.assertEquals(self.sink.__gstrefcount__, 1)
        del self.sink
        TestCase.tearDown(self)

    def _chain_func(self, pad, buffer):
        gst.debug('got buffer %r, id %x, with GMO rc %d'% (
            buffer, id(buffer), buffer.__grefcount__))
        self.buffers.append(buffer)

        return gst.FLOW_OK

    def testNoProbe(self):
        self.buffer = gst.Buffer()
        gst.debug('created new buffer %r, id %x' % (
            self.buffer, id(self.buffer)))
        self.assertEquals(self.buffer.__grefcount__, 1)
        gst.debug('pushing buffer on linked pad, no probe')
        self.assertEquals(self.src.push(self.buffer), gst.FLOW_OK)
        gst.debug('pushed buffer on linked pad, no probe')
        # one refcount is held by our scope, another is held on
        # self.buffers through _chain_func
        self.assertEquals(self.buffer.__grefcount__, 2)
        self.assertEquals(len(self.buffers), 1)
        self.buffers = None
        self.assertEquals(self.buffer.__grefcount__, 1)

    def testFalseProbe(self):
        id = self.src.add_buffer_probe(self._probe_handler, False)
        self.buffer = gst.Buffer()
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.assertEquals(self.src.push(self.buffer), gst.FLOW_OK)
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.src.remove_buffer_probe(id)
        self.assertEquals(len(self.buffers), 0)

    def testTrueProbe(self):
        probe_id = self.src.add_buffer_probe(self._probe_handler, True)
        self.buffer = gst.Buffer()
        self.assertEquals(self.buffer.__grefcount__, 1)
        self.assertEquals(self.src.push(self.buffer), gst.FLOW_OK)
        # one refcount is held by our scope, another is held on
        # self.buffers through _chain_func
        self.assertEquals(self.buffer.__grefcount__, 2)

        # they are not the same Python object ...
        self.failIf(self.buffer is self.buffers[0])
        self.failIf(id(self.buffer) == id(self.buffers[0]))
        # ... but they wrap the same GstBuffer
        self.failUnless(self.buffer == self.buffers[0])
        self.assertEquals(repr(self.buffer), repr(self.buffers[0]))
        
        self.src.remove_buffer_probe(probe_id)
        self.assertEquals(len(self.buffers), 1)
        self.buffers = None
        self.assertEquals(self.buffer.__grefcount__, 1)

    def _probe_handler(self, pad, buffer, ret):
        return ret

# a test to show that we can link a pad from the probe handler

class PadPushProbeLinkTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.src = gst.Pad("src", gst.PAD_SRC)
        self.sink = gst.Pad("sink", gst.PAD_SINK)
        caps = gst.caps_from_string("foo/bar")
        self.src.set_caps(caps)
        self.sink.set_caps(caps)
        self.sink.set_chain_function(self._chain_func)
        self.buffers = []

    def tearDown(self):
        self.assertEquals(sys.getrefcount(self.src), 3)
        self.assertEquals(self.src.__gstrefcount__, 1)
        del self.src
        self.assertEquals(sys.getrefcount(self.sink), 3)
        self.assertEquals(self.sink.__gstrefcount__, 1)
        del self.sink
        TestCase.tearDown(self)

    def _chain_func(self, pad, buffer):
        self.buffers.append(buffer)

        return gst.FLOW_OK

    def testProbeLink(self):
        id = self.src.add_buffer_probe(self._probe_handler)
        self.buffer = gst.Buffer()
        self.assertEquals(self.buffer.__grefcount__, 1)
        gst.debug('pushing buffer on linked pad, no probe')
        self.assertEquals(self.src.push(self.buffer), gst.FLOW_OK)
        gst.debug('pushed buffer on linked pad, no probe')
        # one refcount is held by our scope, another is held on
        # self.buffers through _chain_func
        self.assertEquals(self.buffer.__grefcount__, 2)
        self.assertEquals(len(self.buffers), 1)
        self.buffers = None
        self.assertEquals(self.buffer.__grefcount__, 1)


    def _probe_handler(self, pad, buffer):
        self.src.link(self.sink)
        return True
      
 
class PadTest(TestCase):
    def testConstructor(self):
        # first style uses gst_pad_new
        gst.debug('creating pad with name src')
        pad = gst.Pad("src", gst.PAD_SRC)
        self.failUnless(pad)
        self.assertEquals(sys.getrefcount(pad), 3)
        self.assertEquals(pad.__gstrefcount__, 1)

        gst.debug('creating pad with no name')
        self.failUnless(gst.Pad(None, gst.PAD_SRC))
        self.failUnless(gst.Pad(name=None, direction=gst.PAD_SRC))
        self.failUnless(gst.Pad(direction=gst.PAD_SRC, name=None))
        self.failUnless(gst.Pad(direction=gst.PAD_SRC, name="src"))

        # second uses gst_pad_new_from_template
        #template = gst.PadTemplate()

class PadPipelineTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.pipeline = gst.parse_launch('fakesrc name=source ! fakesink')
        src = self.pipeline.get_by_name('source')
        self.srcpad = src.get_pad('src')

    def tearDown(self):
        del self.pipeline
        del self.srcpad
        TestCase.tearDown(self)
        
# FIXME: now that GstQuery is a miniobject with various _new_ factory
# functions, we need to figure out a way to deal with them in python
#    def testQuery(self):
#        assert self.sink.query(gst.QUERY_TOTAL, gst.FORMAT_BYTES) == -1
#        assert self.srcpad.query(gst.QUERY_POSITION, gst.FORMAT_BYTES) == 0
#        assert self.srcpad.query(gst.QUERY_POSITION, gst.FORMAT_TIME) == 0


class PadProbePipeTest(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.pipeline = gst.Pipeline()
        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.pipeline), 3)

        self.fakesrc = gst.element_factory_make('fakesrc')
        self.fakesink = gst.element_factory_make('fakesink')
        self.assertEquals(self.fakesrc.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)

        self.pipeline.add(self.fakesrc, self.fakesink)
        self.assertEquals(self.fakesrc.__gstrefcount__, 2) # added
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)
        self.assertEquals(self.fakesink.__gstrefcount__, 2) # added
        self.assertEquals(sys.getrefcount(self.fakesink), 3)

        self.fakesrc.link(self.fakesink)

        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.pipeline), 3)
        self.assertEquals(self.fakesrc.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)
        self.assertEquals(self.fakesink.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.fakesink), 3)

    def tearDown(self):
        # Refcount must be either 1 or 2, to allow for a possibly still running
        # state-recalculation thread
        self.assertTrue (self.pipeline.__gstrefcount__ >= 1 and self.pipeline.__gstrefcount__ <= 2)

        self.assertEquals(sys.getrefcount(self.pipeline), 3)
        self.assertEquals(self.fakesrc.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)
        gst.debug('deleting pipeline')
        del self.pipeline
        self.gccollect()

        self.assertEquals(self.fakesrc.__gstrefcount__, 1) # parent gone
        self.assertEquals(self.fakesink.__gstrefcount__, 1) # parent gone
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)
        self.assertEquals(sys.getrefcount(self.fakesink), 3)
        gst.debug('deleting fakesrc')
        del self.fakesrc
        self.gccollect()
        gst.debug('deleting fakesink')
        del self.fakesink
        self.gccollect()

        TestCase.tearDown(self)
        
    def testFakeSrcProbeOnceKeep(self):
        self.fakesrc.set_property('num-buffers', 1)

        self.fakesink.set_property('signal-handoffs', True)
        self.fakesink.connect('handoff', self._handoff_callback_fakesink)

        pad = self.fakesrc.get_pad('src')
        id = pad.add_buffer_probe(self._probe_callback_fakesrc)
        self._got_fakesrc_buffer = 0
        self._got_fakesink_buffer = 0
        self.pipeline.set_state(gst.STATE_PLAYING)
        while not self._got_fakesrc_buffer:
            gst.debug('waiting for fakesrc buffer')
            pass
        while not self._got_fakesink_buffer:
            gst.debug('waiting for fakesink buffer')
            pass

        gst.debug('got buffers from fakesrc and fakesink')
        self.assertEquals(self._got_fakesink_buffer, 1)
        pad.remove_buffer_probe(id)

        self.pipeline.set_state(gst.STATE_NULL)

    def testFakeSrcProbeMany(self):
        self.fakesrc.set_property('num-buffers', 1000)

        pad = self.fakesrc.get_pad('src')
        id = pad.add_buffer_probe(self._probe_callback_fakesrc)
        self._got_fakesrc_buffer = 0
        self.pipeline.set_state(gst.STATE_PLAYING)
        while not self._got_fakesrc_buffer == 1000:
            import time
            # allow for context switching; a busy loop here locks up the
            # streaming thread too much
            time.sleep(.0001)
        pad.remove_buffer_probe(id)

        self.pipeline.set_state(gst.STATE_NULL)

    def _probe_callback_fakesrc(self, pad, buffer):
        self.failUnless(isinstance(pad, gst.Pad))
        self.failUnless(isinstance(buffer, gst.Buffer))
        self._got_fakesrc_buffer += 1
        gst.debug('fakesrc sent buffer %r, %d total sent' % (
            buffer, self._got_fakesrc_buffer))
        return True

    def _handoff_callback_fakesink(self, sink, buffer, pad):
        self.failUnless(isinstance(buffer, gst.Buffer))
        self.failUnless(isinstance(pad, gst.Pad))
        self._got_fakesink_buffer += 1
        gst.debug('fakesink got buffer %r, %d total received' % (
            buffer, self._got_fakesrc_buffer))
        gst.debug('pad %r, py refcount %d, go rc %d, gst rc %d' % (
            pad, sys.getrefcount(pad), pad.__grefcount__, pad.__gstrefcount__))
        return True

    def testRemovingProbe(self):
        self.fakesrc.set_property('num-buffers', 10)

        handle = None
        self._num_times_called = 0
        def buffer_probe(pad, buffer):
            self._num_times_called += 1
            pad.remove_buffer_probe(handle)
            return True

        pad = self.fakesrc.get_pad('src')
        handle = pad.add_buffer_probe(buffer_probe)
        self.pipeline.set_state(gst.STATE_PLAYING)
        m = self.pipeline.get_bus().poll(gst.MESSAGE_EOS, -1)
        assert m
        assert self._num_times_called == 1
        self.pipeline.set_state(gst.STATE_NULL)
        # FIXME: having m going out of scope doesn't seem to be enough
        # to get it gc collected, and it keeps a ref to the pipeline.
        # Look for a way to not have to do this explicitly
        del m
        self.gccollect()

class PadRefCountTest(TestCase):
    def testAddPad(self):
        # add a pad to an element
        e = gst.element_factory_make('fakesrc')
        self.assertEquals(sys.getrefcount(e), 3)
        self.assertEquals(e.__gstrefcount__, 1)

        gst.debug('creating pad with name mypad')
        pad = gst.Pad("mypad", gst.PAD_SRC)
        self.failUnless(pad)
        self.assertEquals(sys.getrefcount(pad), 3)
        self.assertEquals(pad.__gstrefcount__, 1)

        gst.debug('adding pad to element')
        e.add_pad(pad)
        self.assertEquals(sys.getrefcount(e), 3)
        self.assertEquals(e.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(pad), 3)
        self.assertEquals(pad.__gstrefcount__, 2) # added to element

        gst.debug('deleting element and collecting')
        self.gccollect()
        del e
        self.assertEquals(self.gccollect(), 1) # collected the element
        self.assertEquals(sys.getrefcount(pad), 3)
        self.assertEquals(pad.__gstrefcount__, 1) # removed from element

        gst.debug('deleting pad and collecting')
        del pad
        self.assertEquals(self.gccollect(), 1) # collected the pad
        gst.debug('going into teardown')

if __name__ == "__main__":
    unittest.main()
