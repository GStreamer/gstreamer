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
import gc
import gobject

class SrcBin(gst.Bin):
    def prepare(self):
        src = gst.element_factory_make('fakesrc')
        self.add(src)
        pad = src.get_pad("src")
        ghostpad = gst.GhostPad("src", pad)
        self.add_pad(ghostpad)
gobject.type_register(SrcBin)

class SinkBin(gst.Bin):
    def prepare(self):
        sink = gst.element_factory_make('fakesink')
        self.add(sink)
        pad = sink.get_pad("sink")
        ghostpad = gst.GhostPad("sink", pad)
        self.add_pad(ghostpad)
        self.sink = sink

    def connect_handoff(self, cb, *args, **kwargs):
        self.sink.set_property('signal-handoffs', True)
        self.sink.connect('handoff', cb, *args, **kwargs)
        
gobject.type_register(SinkBin)

        
class PipeTest(TestCase):
    def setUp(self):
        gst.info("setUp")
        TestCase.setUp(self)
        self.pipeline = gst.Pipeline()
        self.assertEquals(self.pipeline.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.pipeline), pygobject_2_13 and 2 or 3)

        self.src = SrcBin()
        self.src.prepare()
        self.sink = SinkBin()
        self.sink.prepare()
        self.assertEquals(self.src.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.src), pygobject_2_13 and 2 or 3)
        self.assertEquals(self.sink.__gstrefcount__, 1)
        self.assertEquals(sys.getrefcount(self.sink), pygobject_2_13 and 2 or 3)
        gst.info("end of SetUp")

    def tearDown(self):
        gst.info("tearDown")
        self.assertTrue (self.pipeline.__gstrefcount__ >= 1 and self.pipeline.__gstrefcount__ <= 2)
        self.assertEquals(sys.getrefcount(self.pipeline), pygobject_2_13 and 2 or 3)
        self.assertEquals(self.src.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.src), pygobject_2_13 and 2 or 3)
        self.assertEquals(self.sink.__gstrefcount__, 2)
        self.assertEquals(sys.getrefcount(self.sink), 3)
        gst.debug('deleting pipeline')
        del self.pipeline
        self.gccollect()

        self.assertEquals(self.src.__gstrefcount__, 1) # parent gone
        self.assertEquals(self.sink.__gstrefcount__, 1) # parent gone
        self.assertEquals(sys.getrefcount(self.src), pygobject_2_13 and 2 or 3)
        self.assertEquals(sys.getrefcount(self.sink), pygobject_2_13 and 2 or 3)
        gst.debug('deleting src')
        del self.src
        self.gccollect()
        gst.debug('deleting sink')
        del self.sink
        self.gccollect()

        TestCase.tearDown(self)
        
    def testBinState(self):
        self.pipeline.add(self.src, self.sink)
        self.src.link(self.sink)
        self.sink.connect_handoff(self._sink_handoff_cb)
        self._handoffs = 0

        self.assertTrue(self.pipeline.set_state(gst.STATE_PLAYING) != gst.STATE_CHANGE_FAILURE)
        while True:
            (ret, cur, pen) = self.pipeline.get_state()
            if ret == gst.STATE_CHANGE_SUCCESS and cur == gst.STATE_PLAYING:
                break

        while self._handoffs < 10:
                pass

        self.assertEquals(self.pipeline.set_state(gst.STATE_NULL), gst.STATE_CHANGE_SUCCESS)
        while True:
            (ret, cur, pen) = self.pipeline.get_state()
            if ret == gst.STATE_CHANGE_SUCCESS and cur == gst.STATE_NULL:
                break

##     def testProbedLink(self):
##         self.pipeline.add(self.src)
##         pad = self.src.get_pad("src")
        
##         self.sink.connect_handoff(self._sink_handoff_cb)
##         self._handoffs = 0

##         # FIXME: adding a probe to the ghost pad does not work atm
##         # id = pad.add_buffer_probe(self._src_buffer_probe_cb)
##         realpad = pad.get_target()
##         self._probe_id = realpad.add_buffer_probe(self._src_buffer_probe_cb)

##         self._probed = False
        
##         while True:
##             (ret, cur, pen) = self.pipeline.get_state()
##             if ret == gst.STATE_CHANGE_SUCCESS and cur == gst.STATE_PLAYING:
##                 break

##         while not self._probed:
##             pass

##         while self._handoffs < 10:
##             pass

##         self.pipeline.set_state(gst.STATE_NULL)
##         while True:
##             (ret, cur, pen) = self.pipeline.get_state()
##             if ret == gst.STATE_CHANGE_SUCCESS and cur == gst.STATE_NULL:
##                 break

    def _src_buffer_probe_cb(self, pad, buffer):
        gst.debug("received probe on pad %r" % pad)
        self._probed = True
        gst.debug('adding sink bin')
        self.pipeline.add(self.sink)
        # this seems to get rid of the warnings about pushing on an unactivated
        # pad
        gst.debug('setting sink state')
        
        # FIXME: attempt one: sync to current pending state of bin
        (res, cur, pen) = self.pipeline.get_state(timeout=0)
        target = pen
        if target == gst.STATE_VOID_PENDING:
            target = cur
        gst.debug("setting sink state to %r" % target)
        # FIXME: the following print can cause a lock-up; why ?
        # print target
        # if we don't set async, it will possibly end up in PAUSED
        self.sink.set_state(target)
        
        gst.debug('linking')
        self.src.link(self.sink)
        gst.debug('removing buffer probe id %r' % self._probe_id)
        pad.remove_buffer_probe(self._probe_id)
        self._probe_id = None
        gst.debug('done')

    def _sink_handoff_cb(self, sink, buffer, pad):
        gst.debug('received handoff on pad %r' % pad)
        self._handoffs += 1

class TargetTest(TestCase):
    def test_target(self):
        src = gst.Pad("src", gst.PAD_SRC)

        ghost = gst.GhostPad("ghost_src", src)
        self.failUnless(ghost.get_target() is src)

        ghost.set_target(None)
        self.failUnless(ghost.get_target() is None)

        ghost.set_target(src)
        self.failUnless(ghost.get_target() is src)

if __name__ == "__main__":
    unittest.main()
