#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>

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
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

from common import gst, unittest

class PipelineConstructor(unittest.TestCase):
    def testGoodConstructor(self):
        name = 'test-pipeline'
        pipeline = gst.Pipeline(name)
        assert pipeline is not None, 'pipeline is None'
        assert isinstance(pipeline, gst.Pipeline), 'pipeline is not a GstPipline'
        assert pipeline.get_name() == name, 'pipelines name is wrong'
        
class ThreadConstructor(unittest.TestCase):
    def testCreate(self):
        thread = gst.Thread('test-thread')
        assert thread is not None, 'thread is None'
        assert isinstance(thread, gst.Thread)
        
class Pipeline(unittest.TestCase):
    def setUp(self):
        self.pipeline = gst.Pipeline('test-pipeline')
        source = gst.element_factory_make('fakesrc', 'source')
        source.set_property('num-buffers', 5)
        sink = gst.element_factory_make('fakesink', 'sink')
        self.pipeline.add_many(source, sink)
        gst.element_link_many(source, sink)

    def testRun(self):
        self.assertEqual(self.pipeline.get_state(), gst.STATE_NULL)        
        self.pipeline.set_state(gst.STATE_PLAYING)
        self.assertEqual(self.pipeline.get_state(), gst.STATE_PLAYING)
        
        while self.pipeline.iterate():
            pass

        self.assertEqual(self.pipeline.get_state(), gst.STATE_PAUSED)
        self.pipeline.set_state(gst.STATE_NULL)
        self.assertEqual(self.pipeline.get_state(), gst.STATE_NULL)
        
if __name__ == "__main__":
    unittest.main()
