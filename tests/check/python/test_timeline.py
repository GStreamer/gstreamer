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

from . import overrides_hack

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst  # noqa
from gi.repository import GES  # noqa
import unittest  # noqa
from unittest import mock

from .common import create_main_loop
from .common import create_project
from .common import GESSimpleTimelineTest  # noqa

Gst.init(None)
GES.init()


class TestTimeline(unittest.TestCase):

    def test_signals_not_emitted_when_loading(self):
        mainloop = create_main_loop()
        timeline = create_project(with_group=True, saved=True)

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
        handle = mock.Mock()
        for signal in signals:
            timeline.connect(signal, handle)

        mainloop.run()
        self.assertTrue(loaded_called)
        handle.assert_not_called()


class TestSplitting(GESSimpleTimelineTest):
    def setUp(self):
        self.track_types = [GES.TrackType.AUDIO]
        super(TestSplitting, self).setUp()

    def assertTimelineTopology(self, topology):
        res = []
        for layer in self.timeline.get_layers():
            layer_timings = []
            for clip in layer.get_clips():
                layer_timings.append(
                    (type(clip), clip.props.start, clip.props.duration))

            res.append(layer_timings)

        self.assertEqual(topology, res)
        return res

    def test_spliting_with_auto_transition_on_the_left(self):
        self.timeline.props.auto_transition = True
        clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 100),
                (GES.TransitionClip, 50, 50),
                (GES.TestClip, 50, 100)
            ]
        ])

        clip1.split(25)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 25),
                (GES.TestClip, 25, 75),
                (GES.TransitionClip, 50, 50),
                (GES.TestClip, 50, 100),
            ]
        ])

        clip2.split(125)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 25),
                (GES.TestClip, 25, 75),
                (GES.TransitionClip, 50, 50),
                (GES.TestClip, 50, 75),
                (GES.TestClip, 125, 25),
            ]
        ])


class TestEditing(GESSimpleTimelineTest):

    def test_transition_disappears_when_moving_to_another_layer(self):
        self.timeline.props.auto_transition = True
        unused_clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        self.assertEquals(len(self.layer.get_clips()), 4)

        layer2 = self.timeline.append_layer()
        clip2.edit([], layer2.get_priority(), GES.EditMode.EDIT_NORMAL,
                   GES.Edge.EDGE_NONE, clip2.props.start)
        self.assertEquals(len(self.layer.get_clips()), 1)
        self.assertEquals(len(layer2.get_clips()), 1)

    def test_transition_moves_when_rippling_to_another_layer(self):
        self.timeline.props.auto_transition = True
        clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        all_clips = self.layer.get_clips()
        self.assertEquals(len(all_clips), 4)

        layer2 = self.timeline.append_layer()
        clip1.edit([], layer2.get_priority(), GES.EditMode.EDIT_RIPPLE,
                   GES.Edge.EDGE_NONE, clip1.props.start)
        self.assertEquals(self.layer.get_clips(), [])
        self.assertEquals(set(layer2.get_clips()), set(all_clips))

    def test_transition_rippling_after_next_clip_stays(self):
        self.timeline.props.auto_transition = True
        clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        all_clips = self.layer.get_clips()
        self.assertEquals(len(all_clips), 4)

        clip1.edit([], self.layer.get_priority(), GES.EditMode.EDIT_RIPPLE,
                   GES.Edge.EDGE_NONE, clip2.props.start + 1)
        self.assertEquals(set(self.layer.get_clips()), set(all_clips))

    def test_transition_rippling_over_does_not_create_another_transition(self):
        self.timeline.props.auto_transition = True

        clip1 = self.add_clip(0, 0, 17 * Gst.SECOND)
        clip2 = clip1.split(7.0 * Gst.SECOND)
        # Make a transition between the two clips
        clip1.edit([], self.layer.get_priority(),
                   GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 4.5 * Gst.SECOND)

        # Rippl clip1 and check that transitions ar always the sames
        all_clips = self.layer.get_clips()
        self.assertEquals(len(all_clips), 4)
        clip1.edit([], self.layer.get_priority(), GES.EditMode.EDIT_RIPPLE,
                   GES.Edge.EDGE_NONE, 41.5 * Gst.SECOND)
        self.assertEquals(len(self.layer.get_clips()), 4)
        clip1.edit([], self.layer.get_priority(),
                   GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 35 * Gst.SECOND)
        self.assertEquals(len(self.layer.get_clips()), 4)


class TestSnapping(GESSimpleTimelineTest):

    def test_snapping(self):
        self.timeline.props.auto_transition = True
        self.timeline.set_snapping_distance(1)
        clip1 = self.add_clip(0, 0, 100)

        # Split clip1.
        split_position = 50
        clip2 = clip1.split(split_position)
        self.assertEquals(len(self.layer.get_clips()), 2)
        self.assertEqual(clip1.props.duration, split_position)
        self.assertEqual(clip2.props.start, split_position)

        # Make sure snapping prevents clip2 to be moved to the left.
        clip2.edit([], self.layer.get_priority(), GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE,
                   clip2.props.start - 1)
        self.assertEqual(clip2.props.start, split_position)

    def test_no_snapping_on_split(self):
        self.timeline.props.auto_transition = True
        self.timeline.set_snapping_distance(1)

        not_called = []
        def snapping_started_cb(timeline, element1, element2, dist, self):
            Gst.error("Here %s %s" % (Gst.TIME_ARGS(element1.props.start + element1.props.duration),
                Gst.TIME_ARGS(element2.props.start)))
            not_called.append("No snapping should happen")

        self.timeline.connect('snapping-started', snapping_started_cb, self)
        clip1 = self.add_clip(0, 0, 100)

        # Split clip1.
        split_position = 50
        clip2 = clip1.split(split_position)
        self.assertEqual(not_called, [])
        self.assertEqual(len(self.layer.get_clips()), 2)
        self.assertEqual(clip1.props.duration, split_position)
        self.assertEqual(clip2.props.start, split_position)

class TestTransitions(GESSimpleTimelineTest):

    def test_emission_order_for_transition_clip_added_signal(self):
        self.timeline.props.auto_transition = True
        unused_clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(100, 0, 100)

        # Connect to signals to track in which order they are emitted.
        signals = []

        def clip_added_cb(layer, clip):
            self.assertIsInstance(clip, GES.TransitionClip)
            signals.append("clip-added")
        self.layer.connect("clip-added", clip_added_cb)

        def property_changed_cb(clip, pspec):
            self.assertEqual(clip, clip2)
            self.assertEqual(pspec.name, "start")
            signals.append("notify::start")
        clip2.connect("notify::start", property_changed_cb)

        # Move clip2 to create a transition with clip1.
        clip2.edit([], self.layer.get_priority(),
                   GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 50)
        # The clip-added signal is emitted twice, once for the video
        # transition and once for the audio transition.
        self.assertEqual(
            signals, ["notify::start", "clip-added", "clip-added"])
