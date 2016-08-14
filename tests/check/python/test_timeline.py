# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Alexandru Băluț <alexandru.balut@gmail.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst  # noqa
from gi.repository import GES  # noqa
import unittest  # noqa
from unittest import mock

from . import common

Gst.init(None)
GES.init()


class TestTimeline(unittest.TestCase):

    def test_signals_not_emitted_when_loading(self):
        mainloop = common.create_main_loop()
        timeline = common.create_project(with_group=True, saved=True)

        # Reload the project, check the group.
        project = GES.Project.new(uri=timeline.get_asset().props.uri)

        loaded_called = False
        def loaded(unused_project, unused_timeline):
            nonlocal loaded_called
            loaded_called = True
            mainloop.quit()
        project.connect("loaded", loaded)

        timeline = project.extract()

        signals = ["layer-added", "group-added", "track-added"]
        called = []
        handle = mock.Mock()
        for signal in signals:
            timeline.connect(signal, handle)

        mainloop.run()
        self.assertTrue(loaded_called)
        handle.assert_not_called()
