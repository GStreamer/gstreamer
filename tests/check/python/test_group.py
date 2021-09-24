# -*- coding: utf-8 -*-
#
# Copyright (c) 2015, Thibault Saunier
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

from . import common  # noqa

import unittest  # noqa
from unittest import mock

Gst.init(None)
GES.init()


class TestGroup(common.GESSimpleTimelineTest):

    def testCopyGroup(self):
        clip1 = GES.TestClip.new()
        clip1.props.duration = 10

        self.layer.add_clip(clip1)

        self.assertEqual(len(clip1.get_children(False)), 2)

        group = GES.Group.new()
        self.assertTrue(group.add(clip1))

        self.assertEqual(len(group.get_children(False)), 1)

        group_copy = group.copy(True)
        self.assertEqual(len(group_copy.get_children(False)), 0)

        self.assertTrue(group_copy.paste(10))
        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 2)
        self.assertEqual(clips[1].props.start, 10)

        clips[1].edit([], 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 10)
        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 1)

    def testPasteChangedGroup(self):
        clip1 = GES.TestClip.new()
        clip1.props.duration = 10

        clip2 = GES.TestClip.new()
        clip2.props.start = 20
        clip2.props.duration = 10

        self.layer.add_clip(clip1)
        self.layer.add_clip(clip2)

        self.assertEqual(len(clip1.get_children(False)), 2)

        group = GES.Group.new()
        self.assertTrue(group.add(clip1))

        self.assertEqual(len(group.get_children(False)), 1)

        group_copy = group.copy(True)
        self.assertEqual(len(group_copy.get_children(False)), 0)

        self.assertTrue(group.add(clip2))
        self.assertEqual(len(group.get_children(False)), 2)
        self.assertEqual(len(group_copy.get_children(False)), 0)

        self.assertTrue(group_copy.paste(10))
        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 3)
        self.assertEqual(clips[1].props.start, 10)

    def testPasteChangedGroup(self):
        clip1 = GES.TestClip.new()
        clip1.props.duration = 10

        clip2 = GES.TestClip.new()
        clip2.props.start = 20
        clip2.props.duration = 10

        self.layer.add_clip(clip1)
        self.layer.add_clip(clip2)

        self.assertEqual(len(clip1.get_children(False)), 2)

        group = GES.Group.new()
        self.assertTrue(group.add(clip1))

        self.assertEqual(len(group.get_children(False)), 1)

        group_copy = group.copy(True)
        self.assertEqual(len(group_copy.get_children(False)), 0)

        self.assertTrue(group.add(clip2))
        self.assertEqual(len(group.get_children(False)), 2)
        self.assertEqual(len(group_copy.get_children(False)), 0)

        self.assertTrue(group_copy.paste(10))
        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 3)
        self.assertEqual(clips[1].props.start, 10)

    def test_move_clips_between_layers_with_auto_transition(self):
        self.timeline.props.auto_transition = True
        layer2 = self.timeline.append_layer()
        clip1 = GES.TestClip.new()
        clip1.props.start = 0
        clip1.props.duration = 30

        clip2 = GES.TestClip.new()
        clip2.props.start = 20
        clip2.props.duration = 20

        self.layer.add_clip(clip1)
        self.layer.add_clip(clip2)

        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 4)
        self.assertEqual(layer2.get_clips(), [])

        group = GES.Container.group(clips)
        self.assertIsNotNone(group)

        self.assertTrue(clip1.edit(
            self.timeline.get_layers(), 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 0))
        self.assertEqual(self.layer.get_clips(), [])

        clips = layer2.get_clips()
        self.assertEqual(len(clips), 4)

    def test_remove_emits_signal(self):
        clip1 = GES.TestClip.new()
        self.layer.add_clip(clip1)

        group = GES.Group.new()
        child_removed_cb = mock.Mock()
        group.connect("child-removed", child_removed_cb)

        group.add(clip1)
        group.remove(clip1)
        child_removed_cb.assert_called_once_with(group, clip1)

        group.add(clip1)
        child_removed_cb.reset_mock()
        group.ungroup(recursive=False)
        child_removed_cb.assert_called_once_with(group, clip1)

    def test_loaded_project_has_groups(self):
        mainloop = common.create_main_loop()
        timeline = common.create_project(with_group=True, saved=True)
        layer, = timeline.get_layers()
        group, = timeline.get_groups()
        self.assertEqual(len(layer.get_clips()), 2)
        for clip in layer.get_clips():
            self.assertEqual(clip.get_parent(), group)

        # Reload the project, check the group.
        project = GES.Project.new(uri=timeline.get_asset().props.uri)

        loaded_called = False
        def loaded(unused_project, unused_timeline):
            nonlocal loaded_called
            loaded_called = True
            mainloop.quit()
        project.connect("loaded", loaded)

        timeline = project.extract()

        mainloop.run()
        self.assertTrue(loaded_called)

        layer, = timeline.get_layers()
        group, = timeline.get_groups()
        self.assertEqual(len(layer.get_clips()), 2)
        for clip in layer.get_clips():
            self.assertEqual(clip.get_parent(), group)

    def test_moving_group_with_transition(self):
        self.timeline.props.auto_transition = True
        clip1 = GES.TestClip.new()
        clip1.props.start = 0
        clip1.props.duration = 30

        clip2 = GES.TestClip.new()
        clip2.props.start = 20
        clip2.props.duration = 20

        self.layer.add_clip(clip1)
        self.layer.add_clip(clip2)

        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 4)

        video_transition = None
        audio_transition = None
        for clip in clips:
            if isinstance(clip, GES.TransitionClip):
                if isinstance(clip.get_children(False)[0], GES.VideoTransition):
                    video_transition = clip
                else:
                    audio_transition = clip
        self.assertIsNotNone(audio_transition)
        self.assertIsNotNone(video_transition)

        self.assertEqual(video_transition.props.start, 20)
        self.assertEqual(video_transition.props.duration, 10)
        self.assertEqual(audio_transition.props.start, 20)
        self.assertEqual(audio_transition.props.duration, 10)

        group = GES.Container.group(clips)
        self.assertIsNotNone(group)

        self.assertTrue(clip2.edit(
            self.timeline.get_layers(), 0,
            GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 25))
        clip2.props.start = 25

        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 4)
        self.assertEqual(clip1.props.start, 5)
        self.assertEqual(clip1.props.duration, 30)
        self.assertEqual(clip2.props.start, 25)
        self.assertEqual(clip2.props.duration, 20)

        self.assertEqual(video_transition.props.start, 25)
        self.assertEqual(video_transition.props.duration, 10)
        self.assertEqual(audio_transition.props.start, 25)
        self.assertEqual(audio_transition.props.duration, 10)

    def test_moving_group_snapping_from_the_middle(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        snapped_positions = []
        def snapping_started_cb(timeline, first_element, second_element,
                                position, snapped_positions):
            snapped_positions.append(position)

        self.timeline.props.snapping_distance = 5
        self.timeline.connect("snapping-started", snapping_started_cb,
                              snapped_positions)

        for start in range(0, 20, 5):
            clip = GES.TestClip.new()
            clip.props.start = start
            clip.props.duration = 5
            self.layer.add_clip(clip)

        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 4)

        group = GES.Container.group(clips[1:3])
        self.assertIsNotNone(group)

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 5),
                (GES.TestClip, 5, 5),
                (GES.TestClip, 10, 5),
                (GES.TestClip, 15, 5),
            ],
        ], groups=[clips[1:3]])

        self.assertEqual(clips[1].props.start, 5)
        self.assertEqual(clips[2].props.start, 10)
        clips[2].edit([], 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 11)

        self.assertEqual(snapped_positions[0], 5)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 5),
                (GES.TestClip, 5, 5),
                (GES.TestClip, 10, 5),
                (GES.TestClip, 15, 5),
            ],
        ], groups=[clips[1:3]])

    def test_rippling_with_group(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        for _ in range(4):
            self.append_clip()

        snapped_positions = []
        def snapping_started_cb(timeline, first_element, second_element,
                                position, snapped_positions):
            snapped_positions.append(position)

        self.timeline.props.snapping_distance = 5
        self.timeline.connect("snapping-started", snapping_started_cb,
                              snapped_positions)

        clips = self.layer.get_clips()
        self.assertEqual(len(clips), 4)

        group_clips = clips[1:3]
        GES.Container.group(group_clips)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ],
        ], groups=[group_clips])

        self.assertFalse(clips[2].edit([], 0, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 5))

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ],
        ], groups=[group_clips])

        # Negative start...
        self.assertFalse(clips[2].edit([], 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 1))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ],
        ], groups=[group_clips])

        self.assertTrue(clips[2].edit([], 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 20))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ],
            [
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ],
        ], groups=[group_clips])

    def test_group_priority(self):
        self.track_types = [GES.TrackType.AUDIO]
        self.setUp()

        clip0 = self.append_clip()
        clip1 = self.append_clip(1)
        clip1.props.start = 20

        group = GES.Group.new()
        group.add(clip0)
        group.add(clip1)
        self.assertEqual(group.get_layer_priority(), 0)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ],
            [
                (GES.TestClip, 20, 10),
            ]
        ], groups=[(clip0, clip1)])
        group.remove(clip0)
        self.assertEqual(group.get_layer_priority(), 1)

        clip1.edit(self.timeline.get_layers(), 2, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, clip1.start)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ],
            [ ],
            [
                (GES.TestClip, 20, 10),
            ]
        ], groups=[(clip1,)])

        self.assertEqual(group.get_layer_priority(), 2)
        self.assertTrue(clip1.edit(self.timeline.get_layers(), 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, clip1.start))

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 20, 10),
            ],
            [ ],
            [ ]
        ], groups=[(clip1,)])
