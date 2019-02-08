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

from . import common  # noqa

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
        handle = mock.Mock()
        for signal in signals:
            timeline.connect(signal, handle)

        mainloop.run()
        self.assertTrue(loaded_called)
        handle.assert_not_called()


class TestSplitting(common.GESSimpleTimelineTest):
    def setUp(self):
        self.track_types = [GES.TrackType.AUDIO]
        super(TestSplitting, self).setUp()

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


class TestEditing(common.GESSimpleTimelineTest):

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


class TestSnapping(common.GESSimpleTimelineTest):

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

class TestTransitions(common.GESSimpleTimelineTest):

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

    def create_xges(self):
        uri = common.get_asset_uri("png.png")
        return """<ges version='0.4'>
  <project properties='properties;' metadatas='metadatas, author=(string)&quot;&quot;, render-scale=(double)100, format-version=(string)0.4;'>
    <ressources>
      <asset id='GESTitleClip' extractable-type-name='GESTitleClip' properties='properties;' metadatas='metadatas;' />
      <asset id='bar-wipe-lr' extractable-type-name='GESTransitionClip' properties='properties;' metadatas='metadatas, description=(string)GES_VIDEO_STANDARD_TRANSITION_TYPE_BAR_WIPE_LR;' />
      <asset id='%(uri)s' extractable-type-name='GESUriClip' properties='properties, supported-formats=(int)4, duration=(guint64)18446744073709551615;' metadatas='metadatas, video-codec=(string)PNG, file-size=(guint64)73294;' />
      <asset id='crossfade' extractable-type-name='GESTransitionClip' properties='properties;' metadatas='metadatas, description=(string)GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE;' />
    </ressources>
    <timeline properties='properties, auto-transition=(boolean)true, snapping-distance=(guint64)31710871;' metadatas='metadatas, duration=(guint64)13929667032;'>
      <track caps='video/x-raw(ANY)' track-type='4' track-id='0' properties='properties, async-handling=(boolean)false, message-forward=(boolean)true, caps=(string)&quot;video/x-raw\(ANY\)&quot;, restriction-caps=(string)&quot;video/x-raw\,\ width\=\(int\)1920\,\ height\=\(int\)1080\,\ framerate\=\(fraction\)30/1&quot;, mixing=(boolean)true;' metadatas='metadatas;'/>
      <track caps='audio/x-raw(ANY)' track-type='2' track-id='1' properties='properties, async-handling=(boolean)false, message-forward=(boolean)true, caps=(string)&quot;audio/x-raw\(ANY\)&quot;, restriction-caps=(string)&quot;audio/x-raw\,\ format\=\(string\)S32LE\,\ channels\=\(int\)2\,\ rate\=\(int\)44100\,\ layout\=\(string\)interleaved&quot;, mixing=(boolean)true;' metadatas='metadatas;'/>
      <layer priority='0' properties='properties, auto-transition=(boolean)true;' metadatas='metadatas, volume=(float)1;'>
        <clip id='0' asset-id='%(uri)s' type-name='GESUriClip' layer-priority='0' track-types='6' start='0' duration='4558919302' inpoint='0' rate='0' properties='properties, name=(string)uriclip25263, mute=(boolean)false, is-image=(boolean)false;' />
        <clip id='1' asset-id='bar-wipe-lr' type-name='GESTransitionClip' layer-priority='0' track-types='4' start='3225722559' duration='1333196743' inpoint='0' rate='0' properties='properties, name=(string)transitionclip84;'  children-properties='properties, GESVideoTransition::border=(uint)0, GESVideoTransition::invert=(boolean)false;'/>
        <clip id='2' asset-id='%(uri)s' type-name='GESUriClip' layer-priority='0' track-types='6' start='3225722559' duration='3479110239' inpoint='4558919302' rate='0' properties='properties, name=(string)uriclip25265, mute=(boolean)false, is-image=(boolean)false;' />
      </layer>
      <layer priority='1' properties='properties, auto-transition=(boolean)true;' metadatas='metadatas, volume=(float)1;'>
        <clip id='3' asset-id='%(uri)s' type-name='GESUriClip' layer-priority='1' track-types='4' start='8566459322' duration='1684610449' inpoint='0' rate='0' properties='properties, name=(string)uriclip25266, mute=(boolean)false, is-image=(boolean)true;' />
        <clip id='4' asset-id='GESTitleClip' type-name='GESTitleClip' layer-priority='1' track-types='6' start='8566459322' duration='4500940746' inpoint='0' rate='0' properties='properties, name=(string)titleclip69;' />
        <clip id='5' asset-id='%(uri)s' type-name='GESUriClip' layer-priority='1' track-types='4' start='9566459322' duration='4363207710' inpoint='0' rate='0' properties='properties, name=(string)uriclip25275, mute=(boolean)false, is-image=(boolean)true;' />
      </layer>
      <groups>
      </groups>
    </timeline>
</project>
</ges>""" % {"uri": uri}

    def test_auto_transition(self):
        xges = self.create_xges()
        with common.created_project_file(xges) as proj_uri:
            project = GES.Project.new(proj_uri)
            timeline = project.extract()

            mainloop = common.create_main_loop()
            mainloop.run(until_empty=True)

            layers = timeline.get_layers()
            self.assertEqual(len(layers), 2)

            self.assertTrue(layers[0].props.auto_transition)
            self.assertTrue(layers[1].props.auto_transition)

    def test_transition_type(self):
        xges = self.create_xges()
        with common.created_project_file(xges) as proj_uri:
            project = GES.Project.new(proj_uri)
            timeline = project.extract()

            mainloop = common.create_main_loop()
            mainloop.run(until_empty=True)

            layers = timeline.get_layers()
            self.assertEqual(len(layers), 2)

            clips = layers[0].get_clips()
            clip1 = clips[0]
            clip2 = clips[-1]
            # There should be a transition because clip1 intersects clip2
            self.assertLess(clip1.props.start, clip2.props.start)
            self.assertLess(clip2.props.start, clip1.props.start + clip1.props.duration)
            self.assertLess(clip1.props.start + clip1.props.duration, clip2.props.start + clip2.props.duration)
            self.assertEqual(len(clips), 3)

            # Even though 3 clips overlap 1 transition will be created
            clips = layers[1].get_clips()
            self.assertEqual(len(clips), 4)


class TestPriorities(common.GESSimpleTimelineTest):

    def test_clips_priorities(self):
        clip = self.add_clip(0, 0, 100)
        clip1 = self.add_clip(100, 0, 100)
        self.timeline.commit()

        self.assertLess(clip.props.priority, clip1.props.priority)

        clip.props.start = 101
        self.timeline.commit()
        self.assertGreater(clip.props.priority, clip1.props.priority)
