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

class BusAddWatchTest(TestCase):
    # FIXME: one last remaining ref
    def tearDown(self):
        pass
        
    def testGoodConstructor(self):
        loop = gobject.MainLoop()

        pipeline = gst.parse_launch("fakesrc ! fakesink")
        bus = pipeline.get_bus()
        self.assertEquals(bus.__gstrefcount__, 2)
        self.assertEquals(pipeline.__gstrefcount__, 1)
        watch_id = bus.add_watch(gst.MESSAGE_ANY, self._message_received, pipeline, loop, "one")
        self.assertEquals(bus.__gstrefcount__, 3)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        pipeline.set_state(gst.STATE_PLAYING)

        loop.run()

        pipeline.set_state(gst.STATE_PAUSED)
        loop.run()

        pipeline.set_state(gst.STATE_READY)
        loop.run()

        pipeline.set_state(gst.STATE_NULL)
        self.gccollect()
        self.assertEquals(bus.__gstrefcount__, 3)
        self.assertEquals(pipeline.__gstrefcount__, 1)

        self.failUnless(gobject.source_remove(watch_id))
        self.gccollect()
        self.assertEquals(bus.__gstrefcount__, 2)

        self.assertEquals(pipeline.__gstrefcount__, 1)
        del pipeline
        self.gccollect()

        # flush the bus
        bus.set_flushing(True)
        bus.set_flushing(False)
        self.gccollect()
        # FIXME: refcount is still 2
        # self.assertEquals(bus.__gstrefcount__, 1)

    def _message_received(self, bus, message, pipeline, loop, id):
        self.failUnless(isinstance(bus, gst.Bus))
        self.failUnless(isinstance(message, gst.Message))
        self.assertEquals(id, "one")
        loop.quit()
        return True
        
if __name__ == "__main__":
    unittest.main()
