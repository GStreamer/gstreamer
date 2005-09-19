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

import time

from common import gst, unittest

import gobject

class PipelineConstructor(unittest.TestCase):
    def testGoodConstructor(self):
        name = 'test-pipeline'
        pipeline = gst.Pipeline(name)
        assert pipeline is not None, 'pipeline is None'
        assert isinstance(pipeline, gst.Pipeline), 'pipeline is not a GstPipline'
        assert pipeline.get_name() == name, 'pipelines name is wrong'
        
class Pipeline(unittest.TestCase):
    def setUp(self):
        self.pipeline = gst.Pipeline('test-pipeline')
        source = gst.element_factory_make('fakesrc', 'source')
        source.set_property('num-buffers', 5)
        sink = gst.element_factory_make('fakesink', 'sink')
        self.pipeline.add_many(source, sink)
        gst.element_link_many(source, sink)

    def testRun(self):
        self.assertEqual(self.pipeline.get_state(None)[1], gst.STATE_NULL)
        self.pipeline.set_state(gst.STATE_PLAYING)
        self.assertEqual(self.pipeline.get_state(None)[1], gst.STATE_PLAYING)
        
        time.sleep(1)

        self.assertEqual(self.pipeline.get_state(None)[1], gst.STATE_PLAYING)
        self.pipeline.set_state(gst.STATE_NULL)
        self.assertEqual(self.pipeline.get_state(None)[1], gst.STATE_NULL)

class PipelineAndBus(unittest.TestCase):
    def setUp(self):
        self.pipeline = gst.Pipeline('test-pipeline')
        self.pipeline.set_property('play-timeout', 0L)
        source = gst.element_factory_make('fakesrc', 'source')
        sink = gst.element_factory_make('fakesink', 'sink')
        self.pipeline.add_many(source, sink)
        gst.element_link_many(source, sink)

        self.bus = self.pipeline.get_bus()
        self.bus.add_watch(gst.MESSAGE_ANY, self._message_received)

        self.loop = gobject.MainLoop()

    def _message_received(self, bus, message):
        gst.debug('received message: %s, %s' % (
            message.src.get_path_string(), message.type.value_nicks[1]))
        t = message.type
        if t == gst.MESSAGE_STATE_CHANGED:
            old, new = message.parse_state_changed()
            gst.debug('%r state change from %r to %r' % (
                message.src.get_path_string(), old, new))
            if message.src == self.pipeline and new == gst.STATE_PLAYING:
                self.loop.quit()

        return True

    def testPlaying(self):
        ret = self.pipeline.set_state_async(gst.STATE_PLAYING)
        self.assertEquals(ret, gst.STATE_CHANGE_ASYNC)

        # go into a main loop to wait for messages
        self.loop.run()
        
if __name__ == "__main__":
    unittest.main()
