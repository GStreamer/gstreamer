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

import time

from common import gst, unittest, TestCase, pygobject_2_13

import gobject

class TestConstruction(TestCase):
    def setUp(self):
        self.gctrack()

    def tearDown(self):
        self.gccollect()
        self.gcverify()

    def testGoodConstructor(self):
        name = 'test-pipeline'
        pipeline = gst.Pipeline(name)
        self.assertEquals(pipeline.__gstrefcount__, 1)
        assert pipeline is not None, 'pipeline is None'
        self.failUnless(isinstance(pipeline, gst.Pipeline),
            'pipeline is not a GstPipline')
        assert pipeline.get_name() == name, 'pipelines name is wrong'
        self.assertEquals(pipeline.__gstrefcount__, 1)

    def testParseLaunch(self):
        pipeline = gst.parse_launch('fakesrc ! fakesink')
        
class Pipeline(TestCase):
    def setUp(self):
        self.gctrack()
        self.pipeline = gst.Pipeline('test-pipeline')
        source = gst.element_factory_make('fakesrc', 'source')
        source.set_property('num-buffers', 5)
        sink = gst.element_factory_make('fakesink', 'sink')
        self.pipeline.add(source, sink)
        gst.element_link_many(source, sink)

    def tearDown(self):
        del self.pipeline
        self.gccollect()
        self.gcverify()

    def testRun(self):
        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_NULL)
        self.pipeline.set_state(gst.STATE_PLAYING)
        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_PLAYING)
        
        time.sleep(1)

        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_PLAYING)
        self.pipeline.set_state(gst.STATE_NULL)
        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_NULL)

class PipelineTags(TestCase):
    def setUp(self):
        self.gctrack()
        self.pipeline = gst.parse_launch('audiotestsrc num-buffers=100 ! vorbisenc name=encoder ! oggmux name=muxer ! fakesink')

    def tearDown(self):
        del self.pipeline
        self.gccollect()
        self.gcverify()

    def testRun(self):
        # in 0.10.15.1, this triggers
        # sys:1: gobject.Warning: g_value_get_uint: assertion `G_VALUE_HOLDS_UINT (value)' failed
        # during pipeline playing

        l = gst.TagList()
        l[gst.TAG_ARTIST] = 'artist'
        l[gst.TAG_TRACK_NUMBER] = 1
        encoder = self.pipeline.get_by_name('encoder')
        encoder.merge_tags(l, gst.TAG_MERGE_APPEND)

        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_NULL)
        self.pipeline.set_state(gst.STATE_PLAYING)
        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_PLAYING)
        
        time.sleep(1)

        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_PLAYING)
        self.pipeline.set_state(gst.STATE_NULL)
        self.assertEqual(self.pipeline.get_state()[1], gst.STATE_NULL)


class Bus(TestCase):
    def testGet(self):
        pipeline = gst.Pipeline('test')
        self.assertEquals(pipeline.__gstrefcount__, 1)
        bus = pipeline.get_bus()
        self.assertEquals(pipeline.__gstrefcount__, 1)
        # one for python and one for the pipeline
        self.assertEquals(bus.__gstrefcount__, 2)

        del pipeline
		if not pygobject_2_13:
            self.failUnless(self.gccollect())
        self.assertEquals(bus.__gstrefcount__, 1)
        
class PipelineAndBus(TestCase):
    def setUp(self):
        TestCase.setUp(self)
        self.pipeline = gst.Pipeline('test-pipeline')
        source = gst.element_factory_make('fakesrc', 'source')
        sink = gst.element_factory_make('fakesink', 'sink')
        self.pipeline.add(source, sink)
        gst.element_link_many(source, sink)

        self.bus = self.pipeline.get_bus()
        self.assertEquals(self.bus.__gstrefcount__, 2)
        self.handler = self.bus.add_watch(self._message_received)
        self.assertEquals(self.bus.__gstrefcount__, 3)
        self.assertEquals(self.pipeline.__gstrefcount__, 1)

        self.loop = gobject.MainLoop()

    def tearDown(self):
        # FIXME: fix the refcount issues with the bus/pipeline
        # flush the bus to be able to assert on the pipeline refcount
        #while self.pipeline.__gstrefcount__ > 1:
        self.gccollect()

        # one for the pipeline, two for the snake
        # three for the watch now shake shake shake but don't you
        self.assertEquals(self.bus.__gstrefcount__, 3)
        self.failUnless(gobject.source_remove(self.handler))
        self.assertEquals(self.bus.__gstrefcount__, 2)
        self.gccollect()

        gst.debug('THOMAS: pipeline rc %d' % self.pipeline.__gstrefcount__)
        #self.assertEquals(self.pipeline.__gstrefcount__, 1)
        del self.pipeline
        self.gccollect()
        #self.assertEquals(self.bus.__gstrefcount__, 2)
        del self.bus
        self.gccollect()

        # the async thread can be holding a ref, Wim is going to work on this
        #TestCase.tearDown(self)

    def _message_received(self, bus, message):
        gst.debug('received message: %s, %s' % (
            message.src.get_path_string(), message.type.value_nicks[1]))
        t = message.type
        if t == gst.MESSAGE_STATE_CHANGED:
            old, new, pen = message.parse_state_changed()
            gst.debug('%r state change from %r to %r' % (
                message.src.get_path_string(), old, new))
            if message.src == self.pipeline and new == self.final:
                self.loop.quit()

        return True

    def testPlaying(self):
        self.final = gst.STATE_PLAYING
        ret = self.pipeline.set_state(gst.STATE_PLAYING)
        self.assertEquals(ret, gst.STATE_CHANGE_ASYNC)

        # go into a main loop to wait for messages
        self.loop.run()

        # we go to READY so we get messages; going to NULL would set
        # the bus flushing
        self.final = gst.STATE_READY
        ret = self.pipeline.set_state(gst.STATE_READY)
        self.assertEquals(ret, gst.STATE_CHANGE_SUCCESS)
        self.loop.run()

        # FIXME: not setting to NULL causes a deadlock; we might want to
        # fix this in the bindings
        self.assertEquals(self.pipeline.set_state(gst.STATE_NULL),
            gst.STATE_CHANGE_SUCCESS)
        self.assertEquals(self.pipeline.get_state(),
            (gst.STATE_CHANGE_SUCCESS, gst.STATE_NULL, gst.STATE_VOID_PENDING))
        self.gccollect()

class TestPipeSub(gst.Pipeline):
    def do_handle_message(self, message):
        self.debug('do_handle_message')
        gst.Pipeline.do_handle_message(self, message)
        self.type = message.type
gobject.type_register(TestPipeSub)

class TestPipeSubSub(TestPipeSub):
    def do_handle_message(self, message):
        self.debug('do_handle_message')
        TestPipeSub.do_handle_message(self, message)
gobject.type_register(TestPipeSubSub)


# see http://bugzilla.gnome.org/show_bug.cgi?id=577735
class TestSubClass(TestCase):
    def setUp(self):
        self.gctrack()

    def tearDown(self):
        self.gccollect()
        self.gcverify()

    def testSubClass(self):
        p = TestPipeSub()
        u = gst.element_factory_make('uridecodebin')
        self.assertEquals(u.__grefcount__, 1)
        self.failIf(getattr(p, 'type', None))
        # adding uridecodebin triggers a clock-provide message;
        # this message should be dropped, and thus not affect
        # the refcount of u beyond the parenting.
        p.add(u)
        self.assertEquals(getattr(p, 'type', None), gst.MESSAGE_CLOCK_PROVIDE)
        self.assertEquals(u.__grefcount__, 2)
        del p
        self.assertEquals(u.__grefcount__, 1)

    def testSubSubClass(self):
        # Edward is worried that a subclass of a subclass will screw up
        # the refcounting wrt. GST_BUS_DROP
        p = TestPipeSubSub()
        u = gst.element_factory_make('uridecodebin')
        self.assertEquals(u.__grefcount__, 1)
        self.failIf(getattr(p, 'type', None))
        p.add(u)
        self.assertEquals(getattr(p, 'type', None), gst.MESSAGE_CLOCK_PROVIDE)
        self.assertEquals(u.__grefcount__, 2)
        del p
        self.assertEquals(u.__grefcount__, 1)
     
if __name__ == "__main__":
    unittest.main()
