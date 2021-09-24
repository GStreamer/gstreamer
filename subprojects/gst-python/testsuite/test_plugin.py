# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# gst-python - Python bindings for GStreamer
# Copyright (C) 2007 Johan Dahlin
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

from common import TestCase, unittest

import gi
gi.require_version("Gst", "1.0")
from gi.repository import Gst


class TestPlugin(TestCase):
    def testLoad(self):
        Gst.init(None)
        p = Gst.parse_launch ("fakesrc ! test_identity_py name=id ! fakesink")
        assert p.get_by_name("id").transformed == False
        p.set_state(Gst.State.PLAYING)
        p.get_state(Gst.CLOCK_TIME_NONE)
        p.set_state(Gst.State.NULL)
        assert p.get_by_name("id").transformed == True

if __name__ == "__main__":
    unittest.main()
