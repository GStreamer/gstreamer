# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# Copyright (C) 2025 Netflix Inc.
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

import overrides_hack
overrides_hack

from common import TestCase

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GstApp", "1.0")
from gi.repository import Gst
from gi.repository import GstApp
Gst.init(None)


class TestAppSink(TestCase):

    def test_appsink_object(self):
        # Create an appsink
        appsink = Gst.ElementFactory.make("appsink", None)
        self.assertIsNotNone(appsink)
        self.assertTrue(isinstance(appsink, GstApp.AppSink))
        appsink.set_state(Gst.State.PLAYING)

        # Send an event to the appsink
        pad = appsink.get_static_pad("sink")
        caps = Gst.Caps("audio/x-raw")
        pad.send_event(Gst.Event.new_caps(caps))

        # Send a buffer to the appsink
        pad.chain(Gst.Buffer.new_wrapped([42]))

        # 1st object pulled must be the event
        event = appsink.pull_object()
        self.assertTrue(isinstance(event, Gst.Event))
        self.assertTrue(caps.is_equal(event.parse_caps()))

        # 2nd object pulled must be the buffer
        sample = appsink.pull_object()
        self.assertTrue(isinstance(sample, Gst.Sample))
        buf = sample.get_buffer()
        with buf.map(Gst.MapFlags.READ) as info:
            self.assertEqual(info.data[0], 42)

        appsink.set_state(Gst.State.NULL)
