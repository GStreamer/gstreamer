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
import gc

class PadTemplateTest(TestCase):
    def setUp(self):
        self.gctrack()

    def tearDown(self):
        self.gccollect()
        self.gcverify()
        
    def testConstructor(self):
        template = gst.PadTemplate("template", gst.PAD_SINK,
            gst.PAD_ALWAYS, gst.caps_from_string("audio/x-raw-int"))
        self.failUnless(template)
        self.assertEquals(sys.getrefcount(template), 3)
        #self.assertEquals(template.__gstrefcount__, 1)

class PadTest(TestCase):
    def setUp(self):
        self.gctrack()

    def tearDown(self):
        self.gccollect()
        self.gcverify()
       
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
        self.pipeline = gst.parse_launch('fakesrc name=source ! fakesink')
        src = self.pipeline.get_by_name('source')
        self.srcpad = src.get_pad('src')
        self.gctrack()

    def tearDown(self):
        del self.pipeline
        del self.srcpad
        self.gccollect()
        self.gcverify()

        
# FIXME: now that GstQuery is a miniobject with various _new_ factory
# functions, we need to figure out a way to deal with them in python
#    def testQuery(self):
#        assert self.sink.query(gst.QUERY_TOTAL, gst.FORMAT_BYTES) == -1
#        assert self.srcpad.query(gst.QUERY_POSITION, gst.FORMAT_BYTES) == 0
#        assert self.srcpad.query(gst.QUERY_POSITION, gst.FORMAT_TIME) == 0


class PadProbeTest(TestCase):
    def setUp(self):
        self.gctrack()
        self.pipeline = gst.Pipeline()
        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.pipeline), 3)

        self.fakesrc = gst.element_factory_make('fakesrc')
        self.fakesink = gst.element_factory_make('fakesink')
        self.assertEquals(self.fakesrc.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)

        self.pipeline.add_many(self.fakesrc, self.fakesink)
        self.assertEquals(self.fakesrc.__gstrefcount__, 2) # added
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)

        self.fakesrc.link(self.fakesink)

        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.pipeline), 3)
        self.assertEquals(self.fakesrc.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)

    def tearDown(self):
        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.pipeline), 3)
        self.assertEquals(self.fakesrc.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)
        gst.debug('deleting pipeline')
        del self.pipeline
        self.gccollect()

        self.assertEquals(self.fakesrc.__gstrefcount__, 1) # parent gone
        self.assertEquals(sys.getrefcount(self.fakesrc), 3)
        gst.debug('deleting fakesrc')
        del self.fakesrc
        self.gccollect()
        del self.fakesink
        self.gccollect()

        self.gcverify()
        
    def testFakeSrcProbeOnce(self):
        self.fakesrc.set_property('num-buffers', 1)

        pad = self.fakesrc.get_pad('src')
        id = pad.add_buffer_probe(self._probe_callback_fakesrc)
        self._got_fakesrc_buffer = 0
        self.pipeline.set_state(gst.STATE_PLAYING)
        while not self._got_fakesrc_buffer:
            pass

        self.pipeline.set_state(gst.STATE_NULL)
        pad.remove_buffer_probe (id)

    def testFakeSrcProbeMany(self):
        self.fakesrc.set_property('num-buffers', 1000)

        pad = self.fakesrc.get_pad('src')
        pad.add_buffer_probe(self._probe_callback_fakesrc)
        self._got_fakesrc_buffer = 0
        self.pipeline.set_state(gst.STATE_PLAYING)
        while not self._got_fakesrc_buffer == 1000:
            pass

        self.pipeline.set_state(gst.STATE_NULL)

    def _probe_callback_fakesrc(self, pad, buffer):
        self.failUnless(isinstance(pad, gst.Pad))
        self.failUnless(isinstance(buffer, gst.Buffer))
        self._got_fakesrc_buffer += 1

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
    def setUp(self):
        self.gctrack()

    def tearDown(self):
        self.gccollect()
        self.gcverify()
        
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
        gc.collect()
        del e
        self.assertEquals(gc.collect(), 1) # collected the element
        self.assertEquals(sys.getrefcount(pad), 3)
        self.assertEquals(pad.__gstrefcount__, 1) # removed from element

        gst.debug('deleting pad and collecting')
        del pad
        self.assertEquals(gc.collect(), 1) # collected the pad
        gst.debug('going into teardown')

if __name__ == "__main__":
    unittest.main()
