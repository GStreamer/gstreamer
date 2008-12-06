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

import sys
from common import gobject, gst, unittest, TestCase
import gc

class NewTest(TestCase):
    def testEOS(self):
        gst.info("creating new bin")
        b = gst.Bin()
        gst.info("creating new EOS message from that bin")
        m = gst.message_new_eos(b)
        gst.info("got message : %s" % m)

    def message_application_cb(self, bus, message):
        gst.info("got application message")
        self.got_message = True
        self.loop.quit()

    def testApplication(self):
        self.loop = gobject.MainLoop()
        gst.info("creating new pipeline")
        bin = gst.Pipeline()
        bus = bin.get_bus()
        bus.add_signal_watch()
        self.got_message = False
        bus.connect('message::application', self.message_application_cb)

        struc = gst.Structure("foo")
        msg = gst.message_new_application(bin, struc)
        # the bus is flushing in NULL, so we need to set the pipeline to READY
        bin.set_state(gst.STATE_READY)
        bus.post(msg)
        self.loop.run()
        bus.remove_signal_watch()
        bin.set_state(gst.STATE_NULL)
        self.failUnless(self.got_message == True)
        self.gccollect()

class TestCreateMessages(TestCase):

    def setUp(self):
        TestCase.setUp(self)
        self.element = gst.Bin()

    def tearDown(self):
        del self.element

    def testCustomMessage(self):
        # create two custom messages using the same structure
        s = gst.Structure("something")
        assert s != None
        e1 = gst.message_new_custom(gst.MESSAGE_APPLICATION, self.element, s)
        assert e1
        e2 = gst.message_new_custom(gst.MESSAGE_APPLICATION, self.element, s)
        assert e2

        # make sure the two structures are equal
        self.assertEquals(e1.structure.to_string(),
                          e2.structure.to_string())

    def testTagMessage(self):
        # Create a taglist
        t = gst.TagList()
        t['something'] = "else"
        t['another'] = 42

        # Create two messages using that same taglist
        m1 = gst.message_new_tag(self.element, t)
        assert m1
        m2 = gst.message_new_tag(self.element, t)
        assert m2

        # make sure the two messages have the same taglist
        t1 = m1.parse_tag()
        assert t1
        keys = t1.keys()
        keys.sort()
        self.assertEquals(keys, ['another', 'something'])
        self.assertEquals(t1['something'], "else")
        self.assertEquals(t1['another'], 42)
        t2 = m2.parse_tag()
        assert t2
        keys = t2.keys()
        keys.sort()
        self.assertEquals(keys, ['another', 'something'])
        self.assertEquals(t2['something'], "else")
        self.assertEquals(t2['another'], 42)


if __name__ == "__main__":
    unittest.main()
