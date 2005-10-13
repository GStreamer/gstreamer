# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2005 Thomas Vander Stichele
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

import gobject

class BusSignalTest(TestCase):
    def testGoodConstructor(self):
        loop = gobject.MainLoop()
        gst.info ("creating pipeline")
        pipeline = gst.parse_launch("fakesrc ! fakesink")
        gst.info ("getting bus")
        bus = pipeline.get_bus()
        gst.info ("got bus")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        self.assertEquals(bus.__gstrefcount__, 2)
        self.assertEquals(pipeline.__gstrefcount__, 1)
        gst.info ("about to add a watch on the bus")
        watch_id = bus.connect("message", self._message_received, pipeline, loop, "one")
        bus.add_signal_watch()
        gst.info ("added a watch on the bus")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        self.assertEquals(bus.__gstrefcount__, 3)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        gst.info("setting to playing")
        ret = pipeline.set_state(gst.STATE_PLAYING)
        gst.info("set to playing %s, loop.run" % ret)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        loop.run()

        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("setting to paused")
        ret = pipeline.set_state(gst.STATE_PAUSED)
        gst.info("set to paused %s, loop.run" % ret)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        loop.run()
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))

        gst.info("setting to ready")
        ret = pipeline.set_state(gst.STATE_READY)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("set to READY %s, loop.run" % ret)
        loop.run()

        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("setting to NULL")
        ret = pipeline.set_state(gst.STATE_NULL)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("set to NULL %s" % ret)
        self.gccollect()
        self.assertEquals(bus.__gstrefcount__, 3)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        gst.info("about to remove the watch id")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        bus.remove_signal_watch()
        gst.info("bus watch id removed")
        bus.disconnect(watch_id)
        gst.info("disconnected callback")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        self.gccollect()
        gst.info("pipeliner:%d/%d busr:%d" % (pipeline.__gstrefcount__, pipeline.__grefcount__, bus.__gstrefcount__))
        
        self.assertEquals(bus.__gstrefcount__, 2)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        gst.info("removing pipeline")
        del pipeline
        gst.info("pipeline removed")
        gst.info("busr:%d" % bus.__gstrefcount__)

        self.gccollect()

        # flush the bus
        bus.set_flushing(True)
        bus.set_flushing(False)
        self.gccollect()
        # FIXME: refcount is still 2
        self.assertEquals(bus.__gstrefcount__, 1)

    def _message_received(self, bus, message, pipeline, loop, id):
        self.failUnless(isinstance(bus, gst.Bus))
        self.failUnless(isinstance(message, gst.Message))
        self.assertEquals(id, "one")
        loop.quit()
        return True


    
class BusAddWatchTest(TestCase):

    def testADumbExample(self):
        gst.info("creating pipeline")
        pipeline = gst.parse_launch("fakesrc ! fakesink")
        gst.info("pipeliner:%s" % pipeline.__gstrefcount__)
        bus = pipeline.get_bus()
        gst.info("got bus, pipeliner:%d, busr:%d" % (pipeline.__gstrefcount__,
                                                     bus.__gstrefcount__))
##         watch_id = bus.add_watch(self._message_received, pipeline)
##         gst.info("added watch,  pipeliner:%d, busr:%d" % (pipeline.__gstrefcount__,
##                                                      bus.__gstrefcount__))
##         gobject.source_remove(watch_id)
##         gst.info("removed watch,  pipeliner:%d, busr:%d" % (pipeline.__gstrefcount__,
##                                                      bus.__gstrefcount__))
        
        
    def testGoodConstructor(self):
        loop = gobject.MainLoop()
        gst.info ("creating pipeline")
        pipeline = gst.parse_launch("fakesrc ! fakesink")
        gst.info ("getting bus")
        bus = pipeline.get_bus()
        gst.info ("got bus")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        self.assertEquals(bus.__gstrefcount__, 2)
        self.assertEquals(pipeline.__gstrefcount__, 1)
        gst.info ("about to add a watch on the bus")
        watch_id = bus.add_watch(self._message_received, pipeline, loop, "one")
        gst.info ("added a watch on the bus")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        self.assertEquals(bus.__gstrefcount__, 3)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        gst.info("setting to playing")
        ret = pipeline.set_state(gst.STATE_PLAYING)
        gst.info("set to playing %s, loop.run" % ret)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        loop.run()

        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("setting to paused")
        ret = pipeline.set_state(gst.STATE_PAUSED)
        gst.info("set to paused %s, loop.run" % ret)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        loop.run()
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))

        gst.info("setting to ready")
        ret = pipeline.set_state(gst.STATE_READY)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("set to READY %s, loop.run" % ret)
        loop.run()

        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("setting to NULL")
        ret = pipeline.set_state(gst.STATE_NULL)
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        gst.info("set to NULL %s" % ret)
        self.gccollect()
        self.assertEquals(bus.__gstrefcount__, 3)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        gst.info("about to remove the watch id")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        self.failUnless(gobject.source_remove(watch_id))
        gst.info("bus watch id removed")
        gst.info("pipeliner:%d busr:%d" % (pipeline.__gstrefcount__, bus.__gstrefcount__))
        self.gccollect()
        gst.info("pipeliner:%d/%d busr:%d" % (pipeline.__gstrefcount__, pipeline.__grefcount__, bus.__gstrefcount__))
        
        self.assertEquals(bus.__gstrefcount__, 2)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        gst.info("removing pipeline")
        del pipeline
        gst.info("pipeline removed")
        gst.info("busr:%d" % bus.__gstrefcount__)

        self.gccollect()

        # flush the bus
        bus.set_flushing(True)
        bus.set_flushing(False)
        self.gccollect()
        # FIXME: refcount is still 2
        self.assertEquals(bus.__gstrefcount__, 1)

    def _message_received(self, bus, message, pipeline, loop, id):
        self.failUnless(isinstance(bus, gst.Bus))
        self.failUnless(isinstance(message, gst.Message))
        self.assertEquals(id, "one")
        # doesn't the following line stop the mainloop before the end of the state change ?
        loop.quit()
        return True

        
if __name__ == "__main__":
    unittest.main()
