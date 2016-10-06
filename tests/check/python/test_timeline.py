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

class TestEditing(common.GESSimpleTimelineTest):

    def test_transition_disappears_when_moving_to_another_layer(self):
        self.timeline.props.auto_transition = True
        unused_clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        self.assertEquals(len(self.layer.get_clips()), 4)

        layer2 = self.timeline.append_layer()
        clip2.edit([], layer2.get_priority(), GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, clip2.props.start)
        self.assertEquals(len(self.layer.get_clips()), 1)
        self.assertEquals(len(layer2.get_clips()), 1)

    def test_transition_moves_when_rippling_to_another_layer(self):
        self.timeline.props.auto_transition = True
        clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        all_clips = self.layer.get_clips()
        self.assertEquals(len(all_clips), 4)

        layer2 = self.timeline.append_layer()
        clip1.edit([], layer2.get_priority(), GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, clip1.props.start)
        self.assertEquals(self.layer.get_clips(), [])
        self.assertEquals(set(layer2.get_clips()), set(all_clips))
