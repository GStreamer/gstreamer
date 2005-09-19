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

from common import gst, unittest

import gobject

class BusAddWatchTest(unittest.TestCase):
    def testGoodConstructor(self):
        loop = gobject.MainLoop()

        pipeline = gst.parse_launch("fakesrc ! fakesink")
        bus = pipeline.get_bus()
        watch_id = bus.add_watch(gst.MESSAGE_ANY, self._message_received, pipeline, loop, "one")

        pipeline.set_state(gst.STATE_PLAYING)

        loop.run()

        # flush the bus
        bus.set_flushing(True)
        bus.set_flushing(False)

        pipeline.set_state(gst.STATE_PAUSED)

        loop.run()

    def _message_received(self, bus, message, pipeline, loop, id):
        self.failUnless(isinstance(bus, gst.Bus))
        self.failUnless(isinstance(message, gst.Message))
        self.assertEquals(id, "one")
        loop.quit()
        return True
        
if __name__ == "__main__":
    unittest.main()
