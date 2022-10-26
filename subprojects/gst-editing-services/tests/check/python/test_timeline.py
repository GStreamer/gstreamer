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

import tempfile  # noqa
import gi

gi.require_version("Gst", "1.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst  # noqa
from gi.repository import GES  # noqa
from gi.repository import GLib  # noqa
import unittest  # noqa
from unittest import mock

from . import common  # noqa

Gst.init(None)
GES.init()


class TestTimeline(common.GESSimpleTimelineTest):

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

    def test_deeply_nested_serialization(self):
        deep_timeline = common.create_project(with_group=True, saved="deep")
        deep_project = deep_timeline.get_asset()

        deep_asset = GES.UriClipAsset.request_sync(deep_project.props.id)

        nested_timeline = common.create_project(with_group=False, saved=False)
        nested_project = nested_timeline.get_asset()
        nested_project.add_asset(deep_project)
        nested_timeline.append_layer().add_asset(deep_asset, 0, 0, 5 * Gst.SECOND, GES.TrackType.UNKNOWN)

        uri = "file://%s" % tempfile.NamedTemporaryFile(suffix="-nested.xges").name
        nested_timeline.get_asset().save(nested_timeline, uri, None, overwrite=True)

        asset = GES.UriClipAsset.request_sync(nested_project.props.id)
        project = self.timeline.get_asset()
        project.add_asset(nested_project)
        refclip = self.layer.add_asset(asset, 0, 0, 5 * Gst.SECOND, GES.TrackType.VIDEO)

        uri = "file://%s" % tempfile.NamedTemporaryFile(suffix=".xges").name
        project.save(self.timeline, uri, None, overwrite=True)
        self.assertEqual(len(project.list_assets(GES.Extractable)), 2)

        mainloop = common.create_main_loop()
        def loaded_cb(unused_project, unused_timeline):
            mainloop.quit()
        project.connect("loaded", loaded_cb)

        # Extract again the timeline and compare with previous one.
        timeline = project.extract()
        mainloop.run()
        layer, = timeline.get_layers()
        clip, = layer.get_clips()
        self.assertEqual(clip.props.uri, refclip.props.uri)
        self.assertEqual(timeline.props.duration, self.timeline.props.duration)

        self.assertEqual(timeline.get_asset(), project)
        self.assertEqual(len(project.list_assets(GES.Extractable)), 2)

    def test_iter_timeline(self):
        all_clips = set()
        for l in range(5):
            self.timeline.append_layer()
            for _ in range(5):
                all_clips.add(self.append_clip(l))
        self.assertEqual(set(self.timeline.iter_clips()), all_clips)


    def test_nested_serialization(self):
        nested_timeline = common.create_project(with_group=True, saved=True)
        nested_project = nested_timeline.get_asset()
        layer = nested_timeline.append_layer()

        asset = GES.UriClipAsset.request_sync(nested_project.props.id)
        refclip = self.layer.add_asset(asset, 0, 0, 110 * Gst.SECOND, GES.TrackType.UNKNOWN)
        nested_project.save(nested_timeline, nested_project.props.id, None, True)

        project = self.timeline.get_asset()
        project.add_asset(nested_project)
        uri = "file://%s" % tempfile.NamedTemporaryFile(suffix=".xges").name
        self.assertEqual(len(project.list_assets(GES.Extractable)), 2)
        project.save(self.timeline, uri, None, overwrite=True)
        self.assertEqual(len(project.list_assets(GES.Extractable)), 2)

        mainloop = common.create_main_loop()
        def loaded(unused_project, unused_timeline):
            mainloop.quit()
        project.connect("loaded", loaded)

        # Extract again the timeline and compare with previous one.
        timeline = project.extract()
        mainloop.run()
        layer, = timeline.get_layers()
        clip, = layer.get_clips()
        self.assertEqual(clip.props.uri, refclip.props.uri)
        self.assertEqual(timeline.props.duration, self.timeline.props.duration)

        self.assertEqual(timeline.get_asset(), project)
        self.assertEqual(len(project.list_assets(GES.Extractable)), 2)

    def test_timeline_duration(self):
        self.append_clip()
        self.append_clip()
        clips = self.layer.get_clips()

        self.assertEqual(self.timeline.props.duration, 20)
        self.layer.remove_clip(clips[1])
        self.assertEqual(self.timeline.props.duration, 10)

        self.append_clip()
        self.append_clip()
        clips = self.layer.get_clips()
        self.assertEqual(self.timeline.props.duration, 30)

        group = GES.Container.group(clips[1:])
        self.assertEqual(self.timeline.props.duration, 30)

        group1 = GES.Container.group([])
        group1.add(group)
        self.assertEqual(self.timeline.props.duration, 30)

    def test_spliting_with_auto_transition_on_the_left(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()

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

    @unittest.skipUnless(*common.can_generate_assets())
    def test_auto_transition_type_after_setting_proxy_asset(self):
        self.track_types = [GES.TrackType.VIDEO]
        super().setUp()

        self.timeline.props.auto_transition = True
        with common.created_video_asset() as uri:
            self.append_clip(asset_type=GES.UriClip, asset_id=uri)
            self.append_clip(asset_type=GES.UriClip, asset_id=uri).props.start = 5
            clip1, transition, clip2 = self.layer.get_clips()
            video_transition, = transition.get_children(True)
            video_transition.set_transition_type(GES.VideoStandardTransitionType.BAR_WIPE_LR)
            self.assertEqual(video_transition.get_transition_type(), GES.VideoStandardTransitionType.BAR_WIPE_LR)

            with common.created_video_asset() as uri2:
                proxy_asset = GES.UriClipAsset.request_sync(uri2)
                clip1.set_asset(proxy_asset)
                clip1, transition1, clip2 = self.layer.get_clips()

                video_transition1, = transition1.get_children(True)
                self.assertEqual(video_transition, video_transition1)
                self.assertEqual(video_transition.get_transition_type(), GES.VideoStandardTransitionType.BAR_WIPE_LR)

    def test_frame_info(self):
        self.track_types = [GES.TrackType.VIDEO]
        super().setUp()

        vtrack, = self.timeline.get_tracks()
        vtrack.update_restriction_caps(Gst.Caps("video/x-raw,framerate=60/1"))
        self.assertEqual(self.timeline.get_frame_time(60), Gst.SECOND)

        layer = self.timeline.append_layer()
        asset = GES.Asset.request(GES.TestClip, "framerate=120/1,height=500,width=500,max-duration=f120")
        clip = layer.add_asset( asset, 0, 0, Gst.SECOND, GES.TrackType.UNKNOWN)
        self.assertEqual(clip.get_id(), "GESTestClip, framerate=(fraction)120/1, height=(int)500, width=(int)500, max-duration=(string)f120;")

        test_source, = clip.get_children(True)
        self.assertEqual(test_source.get_natural_size(), (True, 500, 500))
        self.assertEqual(test_source.get_natural_framerate(), (True, 120, 1))
        self.assertEqual(test_source.props.max_duration, Gst.SECOND)
        self.assertEqual(clip.get_natural_framerate(), (True, 120, 1))

        self.assertEqual(self.timeline.get_frame_at(Gst.SECOND), 60)
        self.assertEqual(clip.props.max_duration, Gst.SECOND)

    def test_layer_active(self):
        def check_nle_object_activeness(clip, track_type, active=None, ref_clip=None):
            assert ref_clip is not None or active is not None

            if ref_clip:
                ref_elem, = ref_clip.find_track_elements(None, track_type, GES.Source)
                active = ref_elem.get_nleobject().props.active

            elem, = clip.find_track_elements(None, track_type, GES.Source)
            self.assertIsNotNone(elem)
            self.assertEqual(elem.get_nleobject().props.active, active)

        def get_tracks(timeline):
            for track in self.timeline.get_tracks():
                if track.props.track_type == GES.TrackType.VIDEO:
                    video_track = track
                else:
                    audio_track = track
            return video_track, audio_track


        def check_set_active_for_tracks(layer, active, tracks, expected_changed_tracks):
            callback_called = []
            def _check_active_changed_cb(layer, active, tracks, expected_tracks, expected_active):
                self.assertEqual(set(tracks), set(expected_tracks))
                self.assertEqual(active, expected_active)
                callback_called.append(True)

            layer.connect("active-changed", _check_active_changed_cb, expected_changed_tracks, active)
            self.assertTrue(layer.set_active_for_tracks(active, tracks))
            self.layer.disconnect_by_func(_check_active_changed_cb)
            self.assertEqual(callback_called, [True])

        c0 = self.append_clip()
        check_nle_object_activeness(c0, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c0, GES.TrackType.AUDIO, True)

        elem, = c0.find_track_elements(None, GES.TrackType.AUDIO, GES.Source)
        elem.props.active = False
        check_nle_object_activeness(c0, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c0, GES.TrackType.AUDIO, False)
        self.check_reload_timeline()
        elem.props.active = True

        # Muting audio track
        video_track, audio_track = get_tracks(self.timeline)

        check_set_active_for_tracks(self.layer, False, [audio_track], [audio_track])

        check_nle_object_activeness(c0, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c0, GES.TrackType.AUDIO, False)
        self.check_reload_timeline()

        c1 = self.append_clip()
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, False)

        l1 = self.timeline.append_layer()
        c1.move_to_layer(l1)
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, True)

        self.assertTrue(c1.edit([], self.layer.get_priority(), GES.EditMode.EDIT_NORMAL,
                   GES.Edge.EDGE_NONE, c1.props.start))
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, False)
        self.check_reload_timeline()

        self.assertTrue(self.layer.remove_clip(c1))
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, True)

        self.assertTrue(self.layer.add_clip(c1))
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, False)

        check_set_active_for_tracks(self.layer, True, None, [audio_track])
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, True)

        elem, = c1.find_track_elements(None, GES.TrackType.AUDIO, GES.Source)
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, True)

        # Force deactivating a specific TrackElement
        elem.props.active = False
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, True)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, False)
        self.check_reload_timeline()

        # Try activating a specific TrackElement, that won't change the
        # underlying nleobject activness
        check_set_active_for_tracks(self.layer, False, None, [audio_track, video_track])
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, False)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, False)

        elem.props.active = True
        check_nle_object_activeness(c1, GES.TrackType.VIDEO, False)
        check_nle_object_activeness(c1, GES.TrackType.AUDIO, False)
        self.check_reload_timeline()


class TestEditing(common.GESSimpleTimelineTest):

    def test_transition_disappears_when_moving_to_another_layer(self):
        self.timeline.props.auto_transition = True
        unused_clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        self.assertEqual(len(self.layer.get_clips()), 4)

        layer2 = self.timeline.append_layer()
        clip2.edit([], layer2.get_priority(), GES.EditMode.EDIT_NORMAL,
                   GES.Edge.EDGE_NONE, clip2.props.start)
        self.assertEqual(len(self.layer.get_clips()), 1)
        self.assertEqual(len(layer2.get_clips()), 1)

    def activate_snapping(self):
        self.timeline.set_snapping_distance(5)
        self.snapped_at = []

        def _snapped_cb(timeline, elem1, elem2, position):
            self.snapped_at.append(position)

        def _snapped_end_cb(timeline, elem1, elem2, position):
            if self.snapped_at:  # Ignoring first snap end.
                self.snapped_at.append(Gst.CLOCK_TIME_NONE)

        self.timeline.connect("snapping-started", _snapped_cb)
        self.timeline.connect("snapping-ended", _snapped_end_cb)

    def test_snap_start_snap_end(self):
        clip = self.append_clip()
        self.append_clip()

        self.activate_snapping()
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

        # snap to 20
        clip.props.start = 18
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
            ]
        ])
        self.assertEqual(self.snapped_at, [20])

        # no snapping
        clip.props.start = 30
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 10, 10),
                (GES.TestClip, 30, 10),
            ]
        ])
        self.assertEqual(self.snapped_at, [20, Gst.CLOCK_TIME_NONE])

        # snap to 20
        clip.props.start = 18
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
            ]
        ])
        self.assertEqual(self.snapped_at, [20, Gst.CLOCK_TIME_NONE, 20])
        # snap to 20 again
        clip.props.start = 19
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
            ]
        ])
        self.assertEqual(
            self.snapped_at,
            [20, Gst.CLOCK_TIME_NONE, 20, Gst.CLOCK_TIME_NONE, 20])

    def test_rippling_snaps(self):
        self.timeline.props.auto_transition = True
        self.append_clip()
        clip = self.append_clip()

        self.activate_snapping()
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

        clip.edit([], 0, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 15)
        self.assertEqual(self.snapped_at, [10])
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

        clip.edit([], 0, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 20)
        self.assertEqual(self.snapped_at, [10, Gst.CLOCK_TIME_NONE])
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 20, 10),
            ]
        ])

    def test_transition_moves_when_rippling_to_another_layer(self):
        self.timeline.props.auto_transition = True
        clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        all_clips = self.layer.get_clips()
        self.assertEqual(len(all_clips), 4)

        layer2 = self.timeline.append_layer()
        clip1.edit([], layer2.get_priority(), GES.EditMode.EDIT_RIPPLE,
                   GES.Edge.EDGE_NONE, clip1.props.start)
        self.assertEqual(self.layer.get_clips(), [])
        self.assertEqual(set(layer2.get_clips()), set(all_clips))

    def test_transition_rippling_after_next_clip_stays(self):
        self.timeline.props.auto_transition = True
        clip1 = self.add_clip(0, 0, 100)
        clip2 = self.add_clip(50, 0, 100)
        all_clips = self.layer.get_clips()
        self.assertEqual(len(all_clips), 4)

        clip1.edit([], self.layer.get_priority(), GES.EditMode.EDIT_RIPPLE,
                   GES.Edge.EDGE_NONE, clip2.props.start + 1)
        self.assertEqual(set(self.layer.get_clips()), set(all_clips))

    def test_transition_rippling_over_does_not_create_another_transition(self):
        self.timeline.props.auto_transition = True

        clip1 = self.add_clip(0, 0, 17 * Gst.SECOND)
        clip2 = clip1.split(7.0 * Gst.SECOND)
        # Make a transition between the two clips
        clip1.edit([], self.layer.get_priority(),
                   GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 4.5 * Gst.SECOND)

        # Rippl clip1 and check that transitions ar always the sames
        all_clips = self.layer.get_clips()
        self.assertEqual(len(all_clips), 4)
        clip1.edit([], self.layer.get_priority(), GES.EditMode.EDIT_RIPPLE,
                   GES.Edge.EDGE_NONE, 41.5 * Gst.SECOND)
        self.assertEqual(len(self.layer.get_clips()), 4)
        clip1.edit([], self.layer.get_priority(),
                   GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 35 * Gst.SECOND)
        self.assertEqual(len(self.layer.get_clips()), 4)

    def test_trim_transition(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()

        self.timeline.props.auto_transition = True
        self.add_clip(0, 0, 10)
        self.add_clip(5, 0, 10)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TransitionClip, 5, 5),
                (GES.TestClip, 5, 10),
            ]
        ])
        transition = self.layer.get_clips()[1]
        self.assertTrue(transition.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 7))

        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TransitionClip, 7, 3),
                (GES.TestClip, 7, 8),
            ]
        ])

    def test_trim_start(self):
        clip = self.append_clip()
        self.assertTrue(clip.edit([], -1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_START, 10))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 10, 10),
            ]
        ])

        self.assertFalse(clip.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 0))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 10, 10),
            ]
        ])

    def test_trim_non_core(self):
        clip = self.append_clip()
        self.assertTrue(clip.set_inpoint(12))
        self.assertTrue(clip.set_max_duration(30))
        self.assertEqual(clip.get_duration_limit(), 18)
        for child in clip.get_children(False):
            self.assertEqual(child.get_inpoint(), 12)
            self.assertEqual(child.get_max_duration(), 30)

        effect0 = GES.Effect.new("textoverlay")
        effect0.set_has_internal_source(True)
        self.assertTrue(effect0.set_inpoint(5))
        self.assertTrue(effect0.set_max_duration(20))
        self.assertTrue(clip.add(effect0))
        self.assertEqual(clip.get_duration_limit(), 15)

        effect1 = GES.Effect.new("agingtv")
        effect1.set_has_internal_source(False)
        self.assertTrue(clip.add(effect1))

        effect2 = GES.Effect.new("textoverlay")
        effect2.set_has_internal_source(True)
        self.assertTrue(effect2.set_inpoint(8))
        self.assertTrue(effect2.set_max_duration(18))
        self.assertTrue(clip.add(effect2))
        self.assertEqual(clip.get_duration_limit(), 10)

        effect3 = GES.Effect.new("textoverlay")
        effect3.set_has_internal_source(True)
        self.assertTrue(effect3.set_inpoint(20))
        self.assertTrue(effect3.set_max_duration(22))
        self.assertTrue(effect3.set_active(False))
        self.assertTrue(clip.add(effect3))
        self.assertEqual(clip.get_duration_limit(), 10)

        self.assertTrue(clip.set_start(10))
        self.assertTrue(clip.set_duration(10))

        # cannot trim to a 0 because effect0 would have a negative in-point
        error = None
        try:
            clip.edit_full(-1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 0)
        except GLib.Error as err:
            error = err
        self.assertGESError(error, GES.Error.NEGATIVE_TIME)

        self.assertEqual(clip.start, 10)
        self.assertEqual(clip.inpoint, 12)
        self.assertEqual(effect0.inpoint, 5)
        self.assertEqual(effect1.inpoint, 0)
        self.assertEqual(effect2.inpoint, 8)
        self.assertEqual(effect3.inpoint, 20)

        self.assertTrue(
            clip.edit_full(-1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 5))

        self.assertEqual(clip.start, 5)
        self.assertEqual(clip.duration, 15)
        self.assertEqual(clip.get_duration_limit(), 15)

        for child in clip.get_children(False):
            self.assertEqual(child.start, 5)
            self.assertEqual(child.duration, 15)

        self.assertEqual(clip.inpoint, 7)
        self.assertEqual(effect0.inpoint, 0)
        self.assertEqual(effect1.inpoint, 0)
        self.assertEqual(effect2.inpoint, 3)
        self.assertEqual(effect3.inpoint, 20)

        self.assertTrue(
            clip.edit_full(-1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 15))

        self.assertEqual(clip.start, 15)
        self.assertEqual(clip.duration, 5)
        self.assertEqual(clip.get_duration_limit(), 5)

        for child in clip.get_children(False):
            self.assertEqual(child.start, 15)
            self.assertEqual(child.duration, 5)

        self.assertEqual(clip.inpoint, 17)
        self.assertEqual(effect0.inpoint, 10)
        self.assertEqual(effect1.inpoint, 0)
        self.assertEqual(effect2.inpoint, 13)
        self.assertEqual(effect3.inpoint, 20)

    def test_trim_time_effects(self):
        self.track_types = [GES.TrackType.VIDEO]
        super().setUp()
        clip = self.append_clip(asset_id="max-duration=30")
        self.assertTrue(clip.set_inpoint(12))
        self.assertEqual(clip.get_duration_limit(), 18)

        children = clip.get_children(False)
        self.assertTrue(children)
        self.assertEqual(len(children), 1)

        source = children[0]
        self.assertEqual(source.get_inpoint(), 12)
        self.assertEqual(source.get_max_duration(), 30)

        rate0 = GES.Effect.new("videorate rate=0.25")

        overlay = GES.Effect.new("textoverlay")
        overlay.set_has_internal_source(True)
        self.assertTrue(overlay.set_inpoint(5))
        self.assertTrue(overlay.set_max_duration(16))

        rate1 = GES.Effect.new("videorate rate=2.0")

        self.assertTrue(clip.add(rate0))
        self.assertTrue(clip.add(overlay))
        self.assertTrue(clip.add(rate1))

        #                   source -> rate1 -> overlay -> rate0
        # in-point/max-dur  12-30              5-16
        # internal
        # start/end         12-30     0-9      5-14       0-36
        self.assertEqual(clip.get_duration_limit(), 36)
        self.assertTrue(clip.set_start(40))
        self.assertTrue(clip.set_duration(10))
        self.check_reload_timeline()

        # cannot trim to a 16 because overlay would have a negative in-point
        error = None
        try:
            clip.edit_full(-1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 16)
        except GLib.Error as err:
            error = err
        self.assertGESError(error, GES.Error.NEGATIVE_TIME)

        self.assertEqual(clip.get_start(), 40)
        self.assertEqual(clip.get_duration(), 10)
        self.assertEqual(source.get_inpoint(), 12)
        self.assertEqual(source.get_max_duration(), 30)
        self.assertEqual(overlay.get_inpoint(), 5)
        self.assertEqual(overlay.get_max_duration(), 16)

        self.check_reload_timeline()

        # trim backwards to 20
        self.assertTrue(
            clip.edit_full(-1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 20))

        self.assertEqual(clip.get_start(), 20)
        self.assertEqual(clip.get_duration(), 30)
        # reduced by 10
        self.assertEqual(source.get_inpoint(), 2)
        self.assertEqual(source.get_max_duration(), 30)
        # reduced by 5
        self.assertEqual(overlay.get_inpoint(), 0)
        self.assertEqual(overlay.get_max_duration(), 16)

        # trim forwards to 28
        self.assertTrue(
            clip.edit_full(-1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 28))
        self.assertEqual(clip.get_start(), 28)
        self.assertEqual(clip.get_duration(), 22)
        # increased by 4
        self.assertEqual(source.get_inpoint(), 6)
        self.assertEqual(source.get_max_duration(), 30)
        # increased by 2
        self.assertEqual(overlay.get_inpoint(), 2)
        self.assertEqual(overlay.get_max_duration(), 16)
        self.check_reload_timeline()

    def test_ripple_end(self):
        clip = self.append_clip()
        clip.set_max_duration(20)
        self.append_clip().set_max_duration(10)
        self.append_clip().set_max_duration(10)
        self.print_timeline()
        self.assertTrue(clip.edit([], -1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_END, 20))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 20),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ]
        ])

        self.assertTrue(clip.edit([], -1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_END, 15))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 15),
                (GES.TestClip, 15, 10),
                (GES.TestClip, 25, 10),
            ]
        ])

    def test_move_group_full_overlap(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()

        for _ in range(4):
            self.append_clip()
        clips = self.layer.get_clips()

        self.assertTrue(clips[0].ripple(20))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
                (GES.TestClip, 40, 10),
                (GES.TestClip, 50, 10),
            ]
        ])
        group = GES.Container.group(clips[1:])
        self.print_timeline()
        self.assertFalse(group.edit([], -1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 0))
        self.print_timeline()
        self.assertTimelineTopology([
            [
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
                (GES.TestClip, 40, 10),
                (GES.TestClip, 50, 10),
            ]
        ])

        self.assertFalse(clips[1].edit([], -1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 0))
        self.print_timeline()
        self.assertTimelineTopology([
            [
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
                (GES.TestClip, 40, 10),
                (GES.TestClip, 50, 10),
            ]
        ])

    def test_trim_inside_group(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()

        for _ in range(2):
            self.append_clip()
        clips = self.layer.get_clips()
        group = GES.Container.group(clips)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])
        self.assertEqual(group.props.start, 0)
        self.assertEqual(group.props.duration, 20)

        clips[0].trim(5)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 5, 5),
                (GES.TestClip, 10, 10),
            ]
        ])
        self.assertEqual(group.props.start, 5)
        self.assertEqual(group.props.duration, 15)

        group1 = GES.Group.new ()
        group1.add(group)
        clips[0].trim(0)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])
        self.assertEqual(group.props.start, 0)
        self.assertEqual(group.props.duration, 20)
        self.assertEqual(group1.props.start, 0)
        self.assertEqual(group1.props.duration, 20)

        self.assertTrue(clips[1].edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 15))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 5),
            ]
        ])
        self.assertEqual(group.props.start, 0)
        self.assertEqual(group.props.duration, 15)
        self.assertEqual(group1.props.start, 0)
        self.assertEqual(group1.props.duration, 15)

    def test_trim_end_past_max_duration(self):
        clip = self.append_clip()
        max_duration = clip.props.duration
        clip.set_max_duration(max_duration)
        self.assertTrue(clip.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 5))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 5, 5),
            ]
        ])

        self.assertFalse(clip.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 15))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 5, 5),
            ]
        ])

    def test_illegal_effect_move(self):
        c0 = self.append_clip()
        self.append_clip()
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

        effect = GES.Effect.new("agingtv")
        c0.add(effect)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

        self.assertFalse(effect.set_start(10))
        self.assertEqual(effect.props.start, 0)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])


        self.assertFalse(effect.set_duration(20))
        self.assertEqual(effect.props.duration, 10)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

    def test_moving_overlay_clip_in_group(self):
        c0 = self.append_clip()
        overlay = self.append_clip(asset_type=GES.TextOverlayClip)
        group = GES.Group.new()
        group.add(c0)
        group.add(overlay)

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TextOverlayClip, 10, 10),
            ]
        ], groups=[(c0, overlay)])

        self.assertTrue(overlay.set_start(20))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 10, 10),
                (GES.TextOverlayClip, 20, 10),
            ]
        ], groups=[(c0, overlay)])

    def test_moving_group_in_group(self):
        c0 = self.append_clip()
        overlay = self.append_clip(asset_type=GES.TextOverlayClip)
        group0 = GES.Group.new()
        group0.add(c0)
        group0.add(overlay)

        c1 = self.append_clip()
        group1 = GES.Group.new()
        group1.add(group0)
        group1.add(c1)

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TextOverlayClip, 10, 10),
                (GES.TestClip, 20, 10),
            ]
        ], groups=[(c1, group0), (c0, overlay)])

        self.assertTrue(group0.set_start(10))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 10, 10),
                (GES.TextOverlayClip, 20, 10),
                (GES.TestClip, 30, 10),
            ]
        ], groups=[(c1, group0), (c0, overlay)])
        self.check_element_values(group0, 10, 0, 20)
        self.check_element_values(group1, 10, 0, 30)

    def test_illegal_group_child_move(self):
        clip0 = self.append_clip()
        _clip1 = self.add_clip(20, 0, 10)
        overlay = self.add_clip(20, 0, 10, asset_type=GES.TextOverlayClip)

        group = GES.Group.new()
        group.add(clip0)
        group.add(overlay)

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TextOverlayClip, 20, 10),
                (GES.TestClip, 20, 10),
            ]
        ], groups=[(clip0, overlay),])

        # Can't move as clip0 and clip1 would fully overlap
        self.assertFalse(overlay.set_start(40))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TextOverlayClip, 20, 10),
                (GES.TestClip, 20, 10),
            ]
        ], groups=[(clip0, overlay)])

    def test_child_duration_change(self):
        c0 = self.append_clip()

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ]
        ])
        self.assertTrue(c0.set_duration(40))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 40),
            ]
        ])

        c0.children[0].set_duration(10)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ]
        ])

        self.assertTrue(c0.set_start(40))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 40, 10),
            ]
        ])

        c0.children[0].set_start(10)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 10, 10),
            ]
        ])


class TestInvalidOverlaps(common.GESSimpleTimelineTest):

    def test_adding_or_moving(self):
        clip1 = self.add_clip(start=10, in_point=0, duration=3)
        self.assertIsNotNone(clip1)

        def check_add_move_clip(start, duration):
            self.timeline.props.auto_transition = True
            self.layer.props.auto_transition = True
            clip2 = GES.TestClip()
            clip2.props.start = start
            clip2.props.duration = duration
            self.assertFalse(self.layer.add_clip(clip2))
            self.assertEqual(len(self.layer.get_clips()), 1)

            # Add the clip at a different position.
            clip2.props.start = 25
            self.assertTrue(self.layer.add_clip(clip2))
            self.assertEqual(clip2.props.start, 25)

            # Try to move the second clip by editing it.
            self.assertFalse(clip2.edit([], -1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, start))
            self.assertEqual(clip2.props.start, 25)

            # Try to put it in a group and move the group.
            clip3 = GES.TestClip()
            clip3.props.start = 20
            clip3.props.duration = 1
            self.assertTrue(self.layer.add_clip(clip3))
            group = GES.Container.group([clip3, clip2])
            self.assertTrue(group.props.start, 20)
            self.assertFalse(group.edit([], -1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, start - 5))
            self.assertEqual(group.props.start, 20)
            self.assertEqual(clip3.props.start, 20)
            self.assertEqual(clip2.props.start, 25)

            for clip in group.ungroup(False):
                self.assertTrue(self.layer.remove_clip(clip))

        # clip1 contains...
        check_add_move_clip(start=10, duration=1)
        check_add_move_clip(start=11, duration=1)
        check_add_move_clip(start=12, duration=1)

    def test_splitting(self):
        clip1 = self.add_clip(start=9, in_point=0, duration=3)
        clip2 = self.add_clip(start=10, in_point=0, duration=4)
        clip3 = self.add_clip(start=12, in_point=0, duration=3)

        self.assertIsNone(clip1.split_full(13))
        self.assertIsNone(clip1.split_full(8))
        self.assertIsNone(clip3.split_full(12))
        self.assertIsNone(clip3.split_full(15))

    def _fail_split(self, clip, position):
        split = None
        error = None
        try:
            split = clip.split_full(position)
        except GLib.Error as err:
            error = err
        self.assertGESError(error, GES.Error.INVALID_OVERLAP_IN_TRACK)
        self.assertIsNone(split)

    def test_split_with_transition(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        self.timeline.set_auto_transition(True)

        clip0 = self.add_clip(start=0, in_point=0, duration=50)
        clip1 = self.add_clip(start=20, in_point=0, duration=50)
        clip2 = self.add_clip(start=60, in_point=0, duration=20)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 50),
                (GES.TransitionClip, 20, 30),
                (GES.TestClip, 20, 50),
                (GES.TransitionClip, 60, 10),
                (GES.TestClip, 60, 20),
            ]
        ])

        # Split should fail as the first part of the split
        # would be fully overlapping clip0
        self._fail_split(clip1, 40)

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 50),
                (GES.TransitionClip, 20, 30),
                (GES.TestClip, 20, 50),
                (GES.TransitionClip, 60, 10),
                (GES.TestClip, 60, 20),
            ]
        ])

        # same with end of the clip
        self._fail_split(clip1, 65)

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 50),
                (GES.TransitionClip, 20, 30),
                (GES.TestClip, 20, 50),
                (GES.TransitionClip, 60, 10),
                (GES.TestClip, 60, 20),
            ]
        ])

    def test_changing_duration(self):
        clip1 = self.add_clip(start=9, in_point=0, duration=2)
        clip2 = self.add_clip(start=10, in_point=0, duration=2)

        self.assertFalse(clip1.set_start(10))
        self.assertFalse(clip1.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, clip2.props.start + clip2.props.duration))
        self.assertFalse(clip1.ripple_end(clip2.props.start + clip2.props.duration))
        self.assertFalse(clip1.roll_end(clip2.props.start + clip2.props.duration))

        # clip2's end edge to the left, to decrease its duration.
        self.assertFalse(clip2.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, clip1.props.start + clip1.props.duration))
        self.assertFalse(clip2.ripple_end(clip1.props.start + clip1.props.duration))
        self.assertFalse(clip2.roll_end(clip1.props.start + clip1.props.duration))

        # clip2's start edge to the left, to increase its duration.
        self.assertFalse(clip2.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, clip1.props.start))
        self.assertFalse(clip2.trim(clip1.props.start))

        # clip1's start edge to the right, to decrease its duration.
        self.assertFalse(clip1.edit([], -1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, clip2.props.start))
        self.assertFalse(clip1.trim(clip2.props.start))

    def test_rippling_backward(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        self.maxDiff = None
        for i in range(4):
            self.append_clip()
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ]
        ])

        clip = self.layer.get_clips()[2]
        self.assertFalse(clip.edit([], -1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, clip.props.start - 20))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ]
        ])
        self.assertTrue(clip.edit([], -1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, clip.props.start + 10))

        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
                (GES.TestClip, 30, 10),
                (GES.TestClip, 40, 10),
            ]
        ])

        self.assertFalse(clip.edit([], -1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, clip.props.start -20))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
                (GES.TestClip, 30, 10),
                (GES.TestClip, 40, 10),
            ]
        ])

    def test_rolling(self):
        clip1 = self.add_clip(start=9, in_point=0, duration=2)
        clip2 = self.add_clip(start=10, in_point=0, duration=2)
        clip3 = self.add_clip(start=11, in_point=0, duration=2)

        # Rolling clip1's end -1 would lead to clip3 to overlap 100% with clip2.
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 9, 2),
                (GES.TestClip, 10, 2),
                (GES.TestClip, 11, 2)
            ]
        ])
        self.assertFalse(clip1.edit([], -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, clip1.props.start + clip1.props.duration - 1))
        self.assertFalse(clip1.roll_end(13))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 9, 2),
                (GES.TestClip, 10, 2),
                (GES.TestClip, 11, 2)
            ]
        ])

        # Rolling clip3's start +1 would lead to clip1 to overlap 100% with clip2.
        self.assertFalse(clip3.edit([], -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 12))
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 9, 2),
                (GES.TestClip, 10, 2),
                (GES.TestClip, 11, 2)
            ]
        ])

    def test_layers(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        self.maxDiff = None
        self.timeline.append_layer()

        for i in range(2):
            self.append_clip()
            self.append_clip(1)

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ],
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

        clip = self.layer.get_clips()[0]
        self.assertFalse(clip.edit([], 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 0))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ],
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

    def test_rippling(self):
        self.timeline.remove_track(self.timeline.get_tracks()[0])
        clip1 = self.add_clip(start=9, in_point=0, duration=2)
        clip2 = self.add_clip(start=10, in_point=0, duration=2)
        clip3 = self.add_clip(start=11, in_point=0, duration=2)

        # Rippling clip2's start -2 would bring clip3 exactly on top of clip1.
        self.assertFalse(clip2.edit([], -1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 8))
        self.assertFalse(clip2.ripple(8))

        # Rippling clip1's end -1 would bring clip3 exactly on top of clip2.
        self.assertFalse(clip1.edit([], -1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_END, 8))
        self.assertFalse(clip1.ripple_end(8))

    def test_move_group_to_layer(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        self.append_clip()
        self.append_clip()
        self.append_clip()

        clips = self.layer.get_clips()

        clips[1].props.start += 2
        group = GES.Container.group(clips[1:])
        self.assertTrue(clips[1].edit([], 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE,
            group.props.start))

        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ],
            [
                (GES.TestClip, 12, 10),
                (GES.TestClip, 20, 10),
            ]
        ])

        clips[0].props.start = 15
        self.assertTimelineTopology([
            [
                (GES.TestClip, 15, 10),
            ],
            [
                (GES.TestClip, 12, 10),
                (GES.TestClip, 20, 10),
            ]
        ])

        self.assertFalse(clips[1].edit([], 0, GES.EditMode.EDIT_NORMAL,
            GES.Edge.EDGE_NONE, group.props.start))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 15, 10),
            ],
            [
                (GES.TestClip, 12, 10),
                (GES.TestClip, 20, 10),
            ]
        ])

    def test_copy_paste_overlapping(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        clip = self.append_clip()

        copy = clip.copy(True)
        self.assertIsNone(copy.paste(copy.props.start))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),

            ]
        ])
        copy = clip.copy(True)
        pasted = copy.paste(copy.props.start + 1)
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 1, 10),
            ]
        ])

        pasted.move_to_layer(self.timeline.append_layer())
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ],
            [
                (GES.TestClip, 1, 10),
            ]
        ])

        copy = pasted.copy(True)
        self.assertIsNotNone(copy.paste(pasted.props.start - 1))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ],
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 1, 10),
            ],
        ])

        group = GES.Group.new()
        group.add(clip)

        copied_group = group.copy(True)
        self.assertFalse(copied_group.paste(group.props.start))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
            ],
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 1, 10),
            ],
        ])

    def test_move_group_with_overlaping_clips(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()
        self.append_clip()
        self.append_clip()
        self.append_clip()

        self.timeline.props.auto_transition = True
        clips = self.layer.get_clips()

        clips[1].props.start += 5
        group = GES.Container.group(clips[1:])
        self.assertTimelineTopology([
            [
                (GES.TestClip, 0, 10),
                (GES.TestClip, 15, 10),
                (GES.TransitionClip, 20, 5),
                (GES.TestClip, 20, 10),
            ]
        ])

        clips[0].props.start = 30
        self.assertTimelineTopology([
            [
                (GES.TestClip, 15, 10),
                (GES.TransitionClip, 20, 5),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ]
        ])

        # the 3 clips would overlap
        self.assertFalse(clips[1].edit([], 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 25))
        self.assertTimelineTopology([
            [
                (GES.TestClip, 15, 10),
                (GES.TransitionClip, 20, 5),
                (GES.TestClip, 20, 10),
                (GES.TestClip, 30, 10),
            ]
        ])


class TestConfigurationRules(common.GESSimpleTimelineTest):

    def _try_add_clip(self, start, duration, layer=None, error=None):
        if layer is None:
            layer = self.layer
        asset = GES.Asset.request(GES.TestClip, None)
        found_err = None
        clip = None
        # large inpoint to allow trims
        try:
            clip = layer.add_asset_full(
                asset, start, 1000, duration, GES.TrackType.UNKNOWN)
        except GLib.Error as err:
            found_err = err
        if error is None:
            self.assertIsNotNone(clip)
        else:
            self.assertIsNone(clip)
            self.assertGESError(found_err, error)
        return clip

    def test_full_overlap_add(self):
        clip1 = self._try_add_clip(50, 50)
        self._try_add_clip(50, 50, error=GES.Error.INVALID_OVERLAP_IN_TRACK)
        self._try_add_clip(49, 51, error=GES.Error.INVALID_OVERLAP_IN_TRACK)
        self._try_add_clip(51, 49, error=GES.Error.INVALID_OVERLAP_IN_TRACK)

    def test_triple_overlap_add(self):
        clip1 = self._try_add_clip(0, 50)
        clip2 = self._try_add_clip(40, 50)
        self._try_add_clip(39, 12, error=GES.Error.INVALID_OVERLAP_IN_TRACK)
        self._try_add_clip(30, 30, error=GES.Error.INVALID_OVERLAP_IN_TRACK)
        self._try_add_clip(1, 88, error=GES.Error.INVALID_OVERLAP_IN_TRACK)

    def test_full_overlap_move(self):
        clip1 = self._try_add_clip(0, 50)
        clip2 = self._try_add_clip(50, 50)
        self.assertFalse(clip2.set_start(0))

    def test_triple_overlap_move(self):
        clip1 = self._try_add_clip(0, 50)
        clip2 = self._try_add_clip(40, 50)
        clip3 = self._try_add_clip(100, 60)
        self.assertFalse(clip3.set_start(30))

    def test_full_overlap_move_into_layer(self):
        clip1 = self._try_add_clip(0, 50)
        layer2 = self.timeline.append_layer()
        clip2 = self._try_add_clip(0, 50, layer2)
        res = None
        try:
            res = clip2.move_to_layer_full(self.layer)
        except GLib.Error as error:
            self.assertGESError(error, GES.Error.INVALID_OVERLAP_IN_TRACK)
        self.assertIsNone(res)

    def test_triple_overlap_move_into_layer(self):
        clip1 = self._try_add_clip(0, 50)
        clip2 = self._try_add_clip(40, 50)
        layer2 = self.timeline.append_layer()
        clip3 = self._try_add_clip(30, 30, layer2)
        res = None
        try:
            res = clip3.move_to_layer_full(self.layer)
        except GLib.Error as error:
            self.assertGESError(error, GES.Error.INVALID_OVERLAP_IN_TRACK)
        self.assertIsNone(res)

    def test_full_overlap_trim(self):
        clip1 = self._try_add_clip(0, 50)
        clip2 = self._try_add_clip(50, 50)
        self.assertFalse(clip2.trim(0))
        self.assertFalse(clip1.set_duration(100))

    def test_triple_overlap_trim(self):
        clip1 = self._try_add_clip(0, 20)
        clip2 = self._try_add_clip(10, 30)
        clip3 = self._try_add_clip(30, 20)
        self.assertFalse(clip3.trim(19))
        self.assertFalse(clip1.set_duration(31))

class TestSnapping(common.GESSimpleTimelineTest):

    def test_snapping(self):
        self.timeline.props.auto_transition = True
        self.timeline.set_snapping_distance(1)
        clip1 = self.add_clip(0, 0, 100)

        # Split clip1.
        split_position = 50
        clip2 = clip1.split(split_position)
        self.assertEqual(len(self.layer.get_clips()), 2)
        self.assertEqual(clip1.props.duration, split_position)
        self.assertEqual(clip2.props.start, split_position)

        # Make sure snapping prevents clip2 to be moved to the left.
        clip2.edit([], self.layer.get_priority(), GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE,
                   clip2.props.start - 1)
        self.assertEqual(clip2.props.start, split_position)

    def test_trim_snapps_inside_group(self):
        self.track_types = [GES.TrackType.AUDIO]
        super().setUp()

        self.timeline.props.auto_transition = True
        self.timeline.set_snapping_distance(5)

        snaps = []
        def snapping_started_cb(timeline, element1, element2, dist, self):
            snaps.append(set([element1, element2]))

        self.timeline.connect('snapping-started', snapping_started_cb, self)
        clip = self.append_clip()
        clip1 = self.append_clip()

        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])

        clip1.edit([], self.layer.get_priority(), GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 15)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 10, 10),
            ]
        ])
        self.assertEqual(snaps[0], set([clip.get_children(False)[0], clip1.get_children(False)[0]]))

        clip1.edit([], self.layer.get_priority(), GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 16)
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 0, 10),
                (GES.TestClip, 16, 4),
            ]
        ])

    def test_trim_no_snapping_on_same_clip(self):
        self.timeline.props.auto_transition = True
        self.timeline.set_snapping_distance(1)

        not_called = []
        def snapping_started_cb(timeline, element1, element2, dist, self):
            not_called.append("No snapping should happen")

        self.timeline.connect('snapping-started', snapping_started_cb, self)
        clip = self.append_clip()
        clip.edit([], self.layer.get_priority(), GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 5)
        self.assertEqual(not_called, [])
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 5, 5),
            ]
        ])

        clip.edit([], self.layer.get_priority(), GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 4)
        self.assertEqual(not_called, [])
        self.assertTimelineTopology([
            [  # Unique layer
                (GES.TestClip, 4, 6),
            ]
        ])

    def test_no_snapping_on_split(self):
        self.timeline.props.auto_transition = True
        self.timeline.set_snapping_distance(1)

        not_called = []
        def snapping_started_cb(timeline, element1, element2, dist, self):
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

class TestComplexEditing(common.GESTimelineConfigTest):

    def test_normal_move(self):
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                            .................g1..................
                            :                                   :
                            :                         *=========*
                            :                         |   c0    |
                            :                         *=========*
        ____________________:___________________________________:____
        layer1______________:___________________________________:____
                            :                                   :
                            ..............g0...............     :
                            :                             :     :
                            *=======================*     :     :
                            |           c1          |     :     :
                            *=========*=============*=====*     :
                            :         |        c2         |     :
                            :         *===================*     :
                            :.............................:     :
                            :                                   :
                            :...................................:
        _____________________________________________________________
        layer2_______________________________________________________

            *=============================*
            |              c3             |
            *=============================*
        """
        track = self.add_video_track()
        c0 = self.add_clip("c0", 0, [track], 23, 5)
        c1 = self.add_clip("c1", 1, [track], 10, 12)
        c2 = self.add_clip("c2", 1, [track], 15, 10)
        self.register_auto_transition(c1, c2, track)
        c3 = self.add_clip("c3", 2, [track], 2, 15)
        g0 = self.add_group("g0", [c1, c2])
        g1 = self.add_group("g1", [c0, g0])

        self.assertTimelineConfig()

        # test invalid edits

        # cannot move c0 up one layer because it would cause a triple
        # overlap between c1, c2 and c3 when g0 moves
        self.assertFailEdit(
            c0, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 23,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot move c0, without moving g1, to 21 layer 1 because it
        # would be completely overlapped by c2
        self.assertFailEdit(
            c0, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_START, 20,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot move c1, without moving g1, with end 25 because it
        # would be completely overlapped by c2
        self.assertFailEdit(
            c0, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_END, 25,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot move g0 to layer 0 because it would make c0 go to a
        # negative layer
        self.assertFailEdit(
            g0, 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 10,
            GES.Error.NEGATIVE_LAYER)

        # cannot move c1 for same reason
        error = None
        try:
            c1.move_to_layer_full(self.timeline.get_layer(0))
        except GLib.Error as err:
            error = err
        self.assertGESError(error, GES.Error.NEGATIVE_LAYER)
        self.assertTimelineConfig({}, [])

        # failure with snapping
        self.timeline.set_snapping_distance(1)

        # cannot move to 0 because end edge of c0 would snap with end of
        # c3, making the new start become negative
        self.assertFailEdit(
            g0, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 0,
            GES.Error.NEGATIVE_TIME)

        # cannot move start of c1 to 14 because snapping causes a full
        # overlap with c0
        self.assertFailEdit(
            c1, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_START, 14,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot move end of c2 to 21 because snapping causes a full
        # overlap with c0
        self.assertFailEdit(
            c2, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_END, 21,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # successes
        self.timeline.set_snapping_distance(3)
        # moving c0 also moves g1, along with g0
        # with no snapping, this would result in a triple overlap between
        # c1, c2 and c3, but c2's start edge will snap to the end of c3
        # at a distance of 3 allowing the edit to succeed
        #
        # c1 and c3 have a new transition
        # transition between c1 and c2 is not lost
        #
        # NOTE: there is no snapping between c0, c1 or c2 even though
        # their edges are within distance 2 of each other because they are
        # all moving
        self.assertEdit(
            c0, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 22, 17,
            [c2], [c3],
            {
                c0 : {"start": 25, "layer": 1},
                c1 : {"start": 12, "layer": 2},
                c2 : {"start": 17, "layer": 2},
                g0 : {"start": 12, "layer": 2},
                g1 : {"start": 12, "layer": 1}
            }, [(c3, c1, track)], [])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

        _____________________________________________________________
        layer1_______________________________________________________

                                .................g1..................
                                :                                   :
                                :                         *=========*
                                :                         |   c0    |
                                :                         *=========*
        ________________________:___________________________________:
        layer2__________________:___________________________________:
                                :                                   :
                                ..............g0...............     :
                                :                             :     :
                                *=======================*     :     :
                                |           c1          |     :     :
            *===================*=========*=============*=====*     :
            |              c3             |        c2         |     :
            *=============================*===================*     :
                                :.............................:     :
                                :                                   :
                                :...................................:
        """
        # using EDGE_START we can move without moving parent
        # snap at same position 17 but with c1's end edge to c3's end
        # edge
        # loose transition between c1 and c3
        self.assertEdit(
            g0, 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_START, 5, 17,
            [c1], [c3],
            {
                c1 : {"start": 5, "layer": 0},
                c2 : {"start": 10, "layer": 0},
                g0 : {"start": 5, "layer": 0},
                g1 : {"start": 5, "duration": 25, "layer": 0},
            }, [], [(c3, c1, track)])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                  .........................g1........................
                  :                                                 :
                  ...............g0..............                   :
                  :                             :                   :
                  *=======================*     :                   :
                  |          c1           |     :                   :
                  *=========*=============*=====*                   :
                  :         |         c2        |                   :
                  :         *===================*                   :
                  :.............................:                   :
        __________:_________________________________________________:
        layer1____:_________________________________________________:
                  :                                                 :
                  :                                       *=========*
                  :                                       |   c0    |
                  :                                       *=========*
                  :.................................................:
        _____________________________________________________________
        layer2_______________________________________________________

            *=============================*
            |              c3             |
            *=============================*
        """
        # using EDGE_END we can move without moving parent
        # no snap
        # loose transition between c1 and c2
        self.assertEdit(
            c2, 1, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_END, 21, None,
            [], [],
            {
                c2 : {"duration": 11, "layer": 1},
                g0 : {"duration": 16},
            }, [], [(c1, c2, track)])

        # no snapping when we use move layer
        self.timeline.set_snapping_distance(10)
        self.assertTrue(
            c3.move_to_layer(self.timeline.get_layer(1)))
        self.assertTimelineConfig(
            new_props={c3 : {"layer": 1}}, new_transitions=[(c3, c2, track)])

    def test_ripple(self):
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                      ......................g0.................
                      :                                       :
                      *=========*                             :
                      |    c0   |                             :
                      *=========*                             :
        ______________:_______________________________________:______
        layer1________:_______________________________________:______
                      :                                       :
                      :                         *=============*
                      :                         |      c3     |
                      :                         *=============*
                      :.......................................:
                            *===================*
                            |         c2        |
                  *=========*=========*=========*
                  |        c1         |
                  *===================*
        """
        track = self.add_video_track()
        c0 = self.add_clip("c0", 0, [track], 7, 5)
        c1 = self.add_clip("c1", 1, [track], 5, 10)
        c2 = self.add_clip("c2", 1, [track], 10, 10)
        c3 = self.add_clip("c3", 1, [track], 20, 7)
        self.register_auto_transition(c1, c2, track)
        g0 = self.add_group("g0", [c0, c3])

        self.assertTimelineConfig()

        # test failures

        self.timeline.set_snapping_distance(2)

        # would cause negative layer priority for c0
        self.assertFailEdit(
            c1, 0, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 5,
            GES.Error.NEGATIVE_LAYER)

        # would lead to c2 fully overlapping c3 since c2 does ripple
        # but c3 does not(c3 shares a toplevel with c0, and
        # GES_EDGE_START, same as NORMAL mode, does not move the
        # toplevel
        self.assertFailEdit(
            c2, 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_END, 25,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # would lead to c2 fully overlapping c3 since c2 does not
        # ripple but c3 does
        self.assertFailEdit(
            c0, 0, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_START, 13,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # add two more clips

        c4 = self.add_clip("c4", 2, [track], 17, 8)
        c5 = self.add_clip("c5", 2, [track], 21, 8)
        self.register_auto_transition(c4, c5, track)

        self.assertTimelineConfig()

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                      ......................g0.................
                      :                                       :
                      *=========*                             :
                      |    c0   |                             :
                      *=========*                             :
        ______________:_______________________________________:______
        layer1________:_______________________________________:______
                      :                                       :
                      :                         *=============*
                      :                         |      c3     |
                      :                         *=============*
                      :.......................................:
                            *===================*
                            |         c2        |
                  *=========*=========*=========*
                  |        c1         |
                  *===================*
        _____________________________________________________________
        layer2_______________________________________________________

                                          *===============*
                                          |       c4      |
                                          *=======*=======*=======*
                                                  |       c5      |
                                                  *===============*
        """

        # rippling start of c2 only moves c4 and c5 because c3 is part
        # of a toplevel with an earlier start
        # NOTE: snapping only occurs for the edges of c2, in particular
        # start of c4 does not snap to end of c1
        self.assertEdit(
            c2, 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 8, 7,
            [c2], [c0],
            {
                c2 : {"start": 7},
                c4 : {"start": 14},
                c5 : {"start": 18},
            }, [], [])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                      ......................g0.................
                      :                                       :
                      *=========*                             :
                      |    c0   |                             :
                      *=========*                             :
        ______________:_______________________________________:______
        layer1________:_______________________________________:______
                      :                                       :
                      :                         *=============*
                      :                         |      c3     |
                      :                         *=============*
                      :.......................................:
                      *===================*
                      |         c2        |
                  *===*===============*===*
                  |        c1         |
                  *===================*
        _____________________________________________________________
        layer2_______________________________________________________

                                    *===============*
                                    |       c4      |
                                    *=======*=======*=======*
                                            |       c5      |
                                            *===============*
        """

        # rippling end of c2, only c5 moves
        # NOTE: start edge of c2 does not snap!
        self.assertEdit(
            c2, 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_END, 19, 20,
            [c2], [c3],
            {
                c2 : {"duration": 13},
                c5 : {"start": 21},
            }, [], [])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                      ......................g0.................
                      :                                       :
                      *=========*                             :
                      |    c0   |                             :
                      *=========*                             :
        ______________:_______________________________________:______
        layer1________:_______________________________________:______
                      :                                       :
                      :                         *=============*
                      :                         |      c3     |
                      :                         *=============*
                      :.......................................:
                      *=========================*
                      |            c2           |
                  *===*===============*=========*
                  |        c1         |
                  *===================*
        _____________________________________________________________
        layer2_______________________________________________________

                                    *===============*
                                    |       c4      |
                                    *=============*=*=============*
                                                  |       c5      |
                                                  *===============*
        """

        # everything except c1 moves, and to the next layer
        # end edge of c2 snaps to end of c1
        # NOTE: does not snap to edges of rippled clips
        # NOTE: c4 and c5 do not loose their transition when moving
        # to the new layer
        self.assertEdit(
            c2, 2, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 0, 15,
            [c2], [c1],
            {
                c0 : {"start": 2, "layer": 1},
                c2 : {"start": 2, "layer": 2},
                c3 : {"start": 15, "layer": 2},
                c4 : {"start": 9, "layer": 3},
                c5 : {"start": 16, "layer": 3},
                g0 : {"start": 2, "layer": 1},
            }, [(c0, c1, track)], [(c1, c2, track)])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

        _____________________________________________________________
        layer1_______________________________________________________

                  *===================*
                  |        c1         |
                  *===================*
            ...................g0....................
            *=========*                             :
            |    c0   |                             :
            *=========*                             :
        ____:_______________________________________:________________
        layer2______________________________________:________________
            :                                       :
            :                         *=============*
            :                         |      c3     |
            :                         *=============*
            :.......................................:
            *=========================*
            |            c2           |
            *=========================*
        _____________________________________________________________
        layer3_______________________________________________________

                          *===============*
                          |       c4      |
                          *=============*=*=============*
                                        |       c5      |
                                        *===============*
        """

        # group c1 and c5, and g0 and c2
        g1 = self.add_group("g1", [c1, c5])
        g2 = self.add_group("g2", [g0, c2])
        self.assertTimelineConfig()

        # moving end edge of c0 does not move anything else in the same
        # toplevel g2
        # c5 does not move because it is grouped with c1, which starts
        # earlier than the end edge of c0
        # only c4 moves
        # c0 does not snap to c4's start edge
        self.assertEdit(
            c0, 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_END, 10, None,
            [], [],
            {
                c0 : {"duration": 8},
                c4 : {"start": 12},
            }, [], [])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

        _____________________________________________________________
        layer1_______________________________________________________

                  ..................g1.....................
                  *===================*                   :
                  |        c1         |                   :
                  *===================*                   :
                  :...................................... :
            ...................g2....................   : :
            :..................g0...................:   : :
            *===============*                       :   : :
            |        c0     |                       :   : :
            *===============*                       :   : :
        ____:_______________________________________:___:_:__________
        layer2______________________________________:___:_:__________
            :                                       :   : :
            :                         *=============*   : :
            :                         |      c3     |   : :
            :                         *=============*   : :
            :.......................................:   : :
            *=========================*             :   : :
            |            c2           |             :   : :
            *=========================*             :   : :
            :.......................................:   : :
        ________________________________________________:_:__________
        layer3__________________________________________:_:__________
                                        ................: :
                                        *===============* :
                                        |       c5      | :
                                        *===============* :
                                        :.................:
                                *===============*
                                |       c4      |
                                *===============*
        """

        # rippling start of c5 does not move anything else
        # end edge snaps to start of c4
        self.timeline.set_snapping_distance(1)
        self.assertEdit(
            c5, 0, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_START, 18, None,
            [], [],
            {
                c5 : {"start": 18, "layer": 0},
                g1 : {"layer": 0, "duration": 21},
            }, [], [(c4, c5, track)])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                  ....................g1.....................
                  :                         *===============*
                  :                         |       c5      |
                  :                         *===============*
        __________:_________________________________________:________
        layer1____:_________________________________________:________
                  :                                         :
                  *===================*                     :
                  |        c1         |                     :
                  *===================*                     :
                  :.........................................:
            ...................g2....................
            :..................g0...................:
            *===============*                       :
            |        c0     |                       :
            *===============*                       :
        ____:_______________________________________:________________
        layer2______________________________________:________________
            :                                       :
            :                         *=============*
            :                         |      c3     |
            :                         *=============*
            :.......................................:
            *=========================*             :
            |            c2           |             :
            *=========================*             :
            :.......................................:
        _____________________________________________________________
        layer3_______________________________________________________

                                *===============*
                                |       c4      |
                                *===============*
        """

        # rippling g1 using c5
        # initial position would make c1 go negative, but end edge of c1
        # will snap to end of c0, allowing the edit to succeed
        # c4 also moves because it is after the start of g1
        self.timeline.set_snapping_distance(3)
        self.assertEdit(
            c5, 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_NONE, 12, 10,
            [c1], [c0],
            {
                c5 : {"start": 13, "layer": 1},
                c1 : {"start": 0, "layer": 2},
                g1 : {"start": 0, "layer": 1},
                c4 : {"start": 7, "layer": 4},
            }, [(c1, c2, track)], [(c0, c1, track)])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________
        _____________________________________________________________
        layer1_______________________________________________________

        ....................g1.....................
        :                         *===============*
        :                         |      c5       |
        :                         *===============*
        :   ...................g2.................:..
        :   :..................g0.................:.:
        :   *===============*                     : :
        :   |        c0     |                     : :
        :   *===============*                     : :
        :___:_____________________________________:_:________________
        layer2____________________________________:_:________________
        :   :                                     : :
        *===================*                     : :
        |        c1         |                     : :
        *===================*                     : :
        :...:.....................................: :
            :                         *=============*
            :                         |      c3     |
            :                         *=============*
            :.......................................:
            *=========================*             :
            |            c2           |             :
            *=========================*             :
            :.......................................:
        _____________________________________________________________
        layer3_______________________________________________________
        _____________________________________________________________
        layer4_______________________________________________________

                                *===============*
                                |       c4      |
                                *===============*
        """
        # moving start of c1 will move everything expect c5 because they
        # can snap to c5 since it is not moving
        # c1 and c2 keep transition
        self.assertEdit(
            c1, 1, GES.EditMode.EDIT_RIPPLE, GES.Edge.EDGE_START, 20, 21,
            [c1], [c5],
            {
                c1 : {"start": 21, "layer": 1},
                g1 : {"start": 13, "duration": 18},
                c0 : {"start": 23, "layer": 0},
                c2 : {"start": 23, "layer": 1},
                c3 : {"start": 36, "layer": 1},
                g0 : {"start": 23, "layer": 0},
                g2 : {"start": 23, "layer": 0},
                c4 : {"start": 28, "layer": 3},
            }, [], [])

    def test_trim(self):
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                  ..................g2...................
                  :                                     :
                  :...............g0.............       :
                  :             *===============*       :
                  :             |       a0      |       :
                  :         *===*=====*=========*       :
                  :         |    a1   |         :       :
                  :         *=========*         :       :
                  *===================*         :       :
                  |        v0         |         :       :
                  *===================*         :       :
                  :.............................:       :
        __________:_____________________________________:____________
        layer1____:_____________________________________:____________
                  :                                     :
                  :         ............g1..............:
                  :         :         *=================*
                  :         :         |       a2        |
                  :         :         *=================*
                  :         *===========================*
                  :         |           v1              |
                  :         *===========================*
        __________:_________:___________________________:____________
        layer2____:_________:___________________________:____________
                  *=============*     *=================*
                  |      a3     |     |       a4        |
                  *=============*     *=================*
                  :         :...........................:
                  *=========================*           :
                  |           v2            |           :
                  *=============*===========*===========*
                  :             |           v3          |
                  :             *=======================*
                  :.....................................:
        _____________________________________________________________
        layer3_______________________________________________________

        *===========================*       *===============*
        |            v4             |       |       v5      |
        *===========================*       *===============*
        """
        audio_track = self.add_audio_track()
        video_track = self.add_video_track()
        a0 = self.add_clip("a0", 0, [audio_track], 12, 8, 5, 15)
        a1 = self.add_clip("a1", 0, [audio_track], 10, 5)
        self.register_auto_transition(a1, a0, audio_track)
        a2 = self.add_clip("a2", 1, [audio_track], 15, 9, 7, 19)
        a3 = self.add_clip("a3", 2, [audio_track], 5, 7, 10)
        a4 = self.add_clip("a4", 2, [audio_track], 15, 9)

        v0 = self.add_clip("v0", 0, [video_track], 5, 10, 5)
        v1 = self.add_clip("v1", 1, [video_track], 10, 14)
        v2 = self.add_clip("v2", 2, [video_track], 5, 13, 4)
        v3 = self.add_clip("v3", 2, [video_track], 12, 12)
        self.register_auto_transition(v2, v3, video_track)
        v4 = self.add_clip("v4", 3, [video_track], 0, 13)
        v5 = self.add_clip("v5", 3, [video_track], 18, 8)

        g0 = self.add_group("g0", [a0, a1, v0])
        g1 = self.add_group("g1", [v1, a2, a4])
        g2 = self.add_group("g2", [a3, v2, v3, g0, g1])

        self.assertTimelineConfig()

        # edit failures

        # cannot trim end of g0 to 16 because a0 and a1 would fully
        # overlap
        self.assertFailEdit(
            g0, 1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 15,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot edit to new layer because there would be triple overlaps
        # between v2, v3, v4 and v5
        self.assertFailEdit(
            g2, 1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 20,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot trim g1 end to 14 because it would result in a negative
        # duration for a2 and a4
        self.assertFailEdit(
            g1, 1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 14,
            GES.Error.NEGATIVE_TIME)

        # cannot trim end of v2 below its start
        self.assertFailEdit(
            v2, 2, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 2,
            GES.Error.NEGATIVE_TIME)

        # cannot trim end of g0 because a0's duration-limit would be
        # exceeded
        self.assertFailEdit(
            g0, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 23,
            GES.Error.NOT_ENOUGH_INTERNAL_CONTENT)

        # cannot trim g0 to 12 because a0 and a1 would fully overlap
        self.assertFailEdit(
            g0, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 12,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot trim start of v2 beyond its end point
        self.assertFailEdit(
            v2, 2, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 20,
            GES.Error.NEGATIVE_TIME)

        # with snapping
        self.timeline.set_snapping_distance(4)

        # cannot trim end of g2 to 19 because v1 and v2 would fully
        # overlap after snapping to v5 start edge(18)
        self.assertFailEdit(
            g2, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 19,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot trim g2 to 3 because it would snap to start edge of
        # v4(0), causing v2's in-point to be negative
        self.assertFailEdit(
            g2, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 3,
            GES.Error.NEGATIVE_TIME)

        # success

        self.timeline.set_snapping_distance(2)

        # first trim v4 start
        self.assertEdit(
            v4, 3, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 1, None, [], [],
            {
                v4 : {"start": 1, "in-point": 1, "duration": 12},
            }, [], [])

        # and trim v5 end
        self.assertEdit(
            v5, 3, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 25, 24,
            [v5], [a2, a4, v1, v3],
            {
                v5 : {"duration": 6},
            }, [], [])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                  ..................g2...................
                  :                                     :
                  :...............g0.............       :
                  :             *===============*       :
                  :             |       a0      |       :
                  :         *===*=====*=========*       :
                  :         |    a1   |         :       :
                  :         *=========*         :       :
                  *===================*         :       :
                  |        v0         |         :       :
                  *===================*         :       :
                  :.............................:       :
        __________:_____________________________________:____________
        layer1____:_____________________________________:____________
                  :                                     :
                  :         ............g1..............:
                  :         :         *=================*
                  :         :         |       a2        |
                  :         :         *=================*
                  :         *===========================*
                  :         |           v1              |
                  :         *===========================*
        __________:_________:___________________________:____________
        layer2____:_________:___________________________:____________
                  *=============*     *=================*
                  |      a3     |     |       a4        |
                  *=============*     *=================*
                  :         :...........................:
                  *=========================*           :
                  |           v2            |           :
                  *=============*===========*===========*
                  :             |           v3          |
                  :             *=======================*
                  :.....................................:
        _____________________________________________________________
        layer3_______________________________________________________

          *=========================*       *===========*
          |          v4             |       |     v5    |
          *=========================*       *===========*
        """

        # can trim g2 to 0 even though in-point of v2 is 4 because it will
        # snap to 1. Note, there is only snapping on the start edge
        # everything at the start edge is stretched back
        self.assertEdit(
            g2, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 0, 1,
            [v0, v2, a3], [v4],
            {
                v0 : {"start": 1, "in-point": 1, "duration": 14},
                a3 : {"start": 1, "in-point": 6, "duration": 11},
                v2 : {"start": 1, "in-point": 0, "duration": 17},
                g0 : {"start": 1, "duration": 19},
                g2 : {"start": 1, "duration": 23},
            }, [], [])

        self.timeline.set_snapping_distance(0)

        # trim end to use as a snapping point
        self.assertEdit(
            v4, 3, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 11, None, [], [],
            {
                v4 : {"duration": 10},
            }, [], [])

        self.timeline.set_snapping_distance(2)
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

          ......................g2.......................
          :                                             :
          :................g0....................       :
          :                     *===============*       :
          :                     |       a0      |       :
          :                 *===*=====*=========*       :
          :                 |    a1   |         :       :
          :                 *=========*         :       :
          *===========================*         :       :
          |           v0              |         :       :
          *===========================*         :       :
          :.....................................:       :
        __:_____________________________________________:____________
        layer1__________________________________________:____________
          :                                             :
          :                 ............g1..............:
          :                 :         *=================*
          :                 :         |       a2        |
          :                 :         *=================*
          :                 *===========================*
          :                 |           v1              |
          :                 *===========================*
        __:_________________:___________________________:____________
        layer2______________:___________________________:____________
          *=====================*     *=================*
          |         a3          |     |       a4        |
          *=====================*     *=================*
          :                 :...........................:
          *=================================*           :
          |                v2               |           :
          *=====================*===========*===========*
          :                     |           v3          |
          :                     *=======================*
          :.............................................:
        _____________________________________________________________
        layer3_______________________________________________________

          *===================*             *===========*
          |        v4         |             |     v5    |
          *===================*             *===========*
        """

        # can trim g2 to 12 even though it would cause a0 and a1 to fully
        # overlap because the snapping allows it to succeed
        self.assertEdit(
            g2, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 12, 11,
            [a3, v0, v2], [v4],
            {
                v0 : {"start": 11, "in-point": 11, "duration": 4},
                a1 : {"start": 11, "in-point": 1, "duration": 4},
                v1 : {"start": 11, "in-point": 1, "duration": 13},
                a3 : {"start": 11, "in-point": 16, "duration": 1},
                v2 : {"start": 11, "in-point": 10, "duration": 7},
                g0 : {"start": 11, "duration": 9},
                g1 : {"start": 11, "duration": 13},
                g2 : {"start": 11, "duration": 13},
            }, [], [])

        # trim end to use as a snapping point
        self.assertEdit(
            v5, 4, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 27, None, [], [],
            {
                v5 : {"duration": 9, "layer": 4},
            }, [], [])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                              .............g2............
                              :                         :
                              :.......g0.........       :
                              : *===============*       :
                              : |       a0      |       :
                              *=*=====*=========*       :
                              |  a1   |         :       :
                              *=======*         :       :
                              *=======*         :       :
                              |  v0   |         :       :
                              *=======*         :       :
                              :.................:       :
        ______________________:_________________________:____________
        layer1________________:_________________________:____________
                              :                         :
                              :.........g1..............:
                              :       *=================*
                              :       |       a2        |
                              :       *=================*
                              *=========================*
                              |         v1              |
                              *=========================*
        ______________________:_________________________:____________
        layer2________________:_________________________:____________
                              *=*     *=================*
                              a3|     |       a4        |
                              *=*     *=================*
                              :.........................:
                              *=============*           :
                              |      v2     |           :
                              *=*===========*===========*
                              : |           v3          |
                              : *=======================*
                              :.........................:
        _____________________________________________________________
        layer3_______________________________________________________

          *===================*
          |        v4         |
          *===================*
        _____________________________________________________________
        layer4_______________________________________________________

                                            *=================*
                                            |         v5      |
                                            *=================*
        """

        # trim end of g2 and move layer. Without the snap, would fail since
        # a2's duration-limit is 12.
        # Even elements not being trimmed will still move layer
        # a0 and a1 keep transition
        # v2 and v3 keep transition
        self.assertEdit(
            g2, 1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 29, 27,
            [a2, a4, v1, v3], [v5],
            {
                a2 : {"duration": 12, "layer": 2},
                v1 : {"duration": 16, "layer": 2},
                a4 : {"duration": 12, "layer": 3},
                v3 : {"duration": 15, "layer": 3},
                g1 : {"duration": 16, "layer": 2},
                g2 : {"duration": 16, "layer": 1},
                a0 : {"layer": 1},
                a1 : {"layer": 1},
                v0 : {"layer": 1},
                a3 : {"layer": 3},
                v2 : {"layer": 3},
                g0 : {"layer": 1},
            }, [], [])

        # trim start to use as a snapping point
        self.timeline.set_snapping_distance(0)
        self.assertEdit(
            v5, 4, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 19, None,
            [], [],
            {
                v5 : {"start": 19, "in-point": 1, "duration": 8},
            }, [], [])

        self.timeline.set_snapping_distance(2)
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________
        _____________________________________________________________
        layer1_______________________________________________________

                              .............g2..................
                              :                               :
                              :.......g0.........             :
                              : *===============*             :
                              : |       a0      |             :
                              *=*=====*=========*             :
                              |  a1   |         :             :
                              *=======*         :             :
                              *=======*         :             :
                              |  v0   |         :             :
                              *=======*         :             :
                              :.................:             :
        ______________________:_______________________________:______
        layer2________________:_______________________________:______
                              :                               :
                              :.........g1....................:
                              :       *=======================*
                              :       |           a2          |
                              :       *=======================*
                              *===============================*
                              |               v1              |
                              *===============================*
        ______________________:_______________________________:______
        layer3________________:_______________________________:______
                              *=*     *=======================*
                              a3|     |           a4          |
                              *=*     *=======================*
                              :...............................:
          *===================*=============*                 :
          |        v4         |      v2     |                 :
          *===================*=*===========*=================*
                              : |                v3           |
                              : *=============================*
                              :...............................:
        _____________________________________________________________
        layer4_______________________________________________________

                                              *===============*
                                              |       v5      |
                                              *===============*
        """

        # trim end of g2 and move layer. Trim at 17 would lead to
        # v3 being fully overlapped by v2, but snap to 19 makes it work
        self.assertEdit(
            g2, 1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 17, 19,
            [a2, a4, v1, v3], [v5],
            {
                a0 : {"duration": 7},
                a2 : {"duration": 4},
                v1 : {"duration": 8},
                a4 : {"duration": 4},
                v3 : {"duration": 7},
                g0 : {"duration": 8},
                g1 : {"duration": 8},
                g2 : {"duration": 8},
            }, [], [])

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________
        _____________________________________________________________
        layer1_______________________________________________________

                              ........g2.......
                              :               :
                              :.......g0......:
                              : *=============*
                              : |      a0     |
                              *=*=====*=======*
                              |  a1   |       :
                              *=======*       :
                              *=======*       :
                              |  v0   |       :
                              *=======*       :
                              :...............:
        ______________________:_______________:______________________
        layer2________________:_______________:______________________
                              :               :
                              :.......g1......:
                              :       *=======*
                              :       |   a2  |
                              :       *=======*
                              *===============*
                              |       v1      |
                              *===============*
        ______________________:_______________:______________________
        layer3________________:_______________:______________________
                              *=*     *=======*
                              a3|     |   a4  |
                              *=*     *=======*
                              :...............:
          *===================*=============* :
          |        v4         |      v2     | :
          *===================*=*===========*=*
                              : |     v3      |
                              : *=============*
                              :...............:
        _____________________________________________________________
        layer4_______________________________________________________

                                              *===============*
                                              |       v5      |
                                              *===============*
        """

        # can trim without trimming parent
        self.assertEdit(
            v0, 1, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 5, None, [], [],
            {
                v0 : {"start": 5, "in-point": 5, "duration": 10},
                g0 : {"start": 5, "duration": 14},
                g2 : {"start": 5, "duration": 14},
            }, [], [])

        self.assertEdit(
            a2, 2, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 23, None, [], [],
            {
                a2 : {"duration": 8},
                g1 : {"duration": 12},
                g2 : {"duration": 18},
            }, [], [])
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________
        _____________________________________________________________
        layer1_______________________________________________________

                  ...................g2................
                  :                                   :
                  :...........g0...............       :
                  :             *=============*       :
                  :             |      a0     |       :
                  :           *=*=====*=======*       :
                  :           |  a1   |       :       :
                  :           *=======*       :       :
                  *===================*       :       :
                  |         v0        |       :       :
                  *===================*       :       :
                  :...........................:       :
        __________:___________________________________:______________
        layer2____:___________________________________:______________
                  :                                   :
                  :           ............g1..........:
                  :           :       *===============*
                  :           :       |       a2      |
                  :           :       *===============*
                  :           *===============*       :
                  :           |       v1      |       :
                  :           *===============*       :
        __________:___________:_______________________:______________
        layer3____:___________:_______________________:______________
                  :           *=*     *=======*       :
                  :           a3|     |   a4  |       :
                  :           *=*     *=======*       :
                  :           :.......................:
          *===================*=============*         :
          |        v4         |      v2     |         :
          *===================*=*===========*=*       :
                  :             |     v3      |       :
                  :             *=============*       :
                  :...................................:
        _____________________________________________________________
        layer4_______________________________________________________

                                              *===============*
                                              |       v5      |
                                              *===============*
        """
        # same with group within a group
        self.assertEdit(
            g0, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_START, 9, 11,
            [v0], [v1, v2, v4, a3],
            {
                v0 : {"start": 11, "in-point": 11, "duration": 4, "layer": 0},
                a0 : {"layer": 0},
                a1 : {"layer": 0},
                g0 : {"start": 11, "duration": 8, "layer": 0},
                g2 : {"start": 11, "duration": 12, "layer": 0},
            }, [], [])

        self.assertEdit(
            g0, 0, GES.EditMode.EDIT_TRIM, GES.Edge.EDGE_END, 17, 18,
            [a0], [v2],
            {
                a0 : {"duration": 6},
                g0 : {"duration": 7},
            }, [], [])


    def test_roll(self):
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                      *===============================*                ]
                      |               c0              |                ]
        *=============*=====*===================*=====*=============*  ]>video
        |        c1         |                   |         c2        |  ]
        *===================*                   *===================*  ]
                            ..........g0.........
                            *===========*       :                      ]
                            |     c3    |       :                      ]>audio0
                            *===*=======*===*   :                      ]
                            :   |    c4     |   :                      ] ]
                            :   *===*=======*===*                        ]
                            :       |    c5     |                        ]>audio1
                            :       *===========*                        ]
        ____________________:___________________:____________________
        layer1______________:___________________:____________________
                            :                   :
        .............g3.....:....               :
        :     *=================*               :                      ]
        :     |      c12        |           ....:...g4...............  ]
        :     *=================*           :   :                   :  ]
        :........g1.........:   :           :   :.........g2........:  ]>audio0
        *=============*     :   :           :   *=============*     :  ]
        |     c8      |     :   :           :   |      c10    |     :  ]
        *=============*     :   :     *=========*=============*     :  ]
        :                   :   :     |   c7    |                   :  ] ]
        :                   *=========*=========*                   :    ]>video
        :                   |    c6   |     :   :                   :  ] ]
        :     *=============*=========*     :   :     *=============*  ]
        :     |     c9      |...:...........:...:     |     c11     |  ]
        :     *=============*   :           :   :     *=============*  ]
        :...................:   :           :   :...................:  ]>audio1
        :.......................:           *=================*     :  ]
                                            |        c13      |     :  ]
                                            *=================*     :  ]
                                            :.......................:
        """
        video = self.add_video_track()
        audio0 = self.add_audio_track()
        audio1 = self.add_audio_track()

        c0 = self.add_clip("c0", 0, [video], 7, 16)
        c1 = self.add_clip("c1", 0, [video], 0, 10)
        c2 = self.add_clip("c2", 0, [video], 20, 10, 20)
        self.register_auto_transition(c1, c0, video)
        self.register_auto_transition(c0, c2, video)

        c3 = self.add_clip("c3", 0, [audio0], 10, 6, 2, 38)
        c4 = self.add_clip("c4", 0, [audio0, audio1], 12, 6, 15)
        self.register_auto_transition(c3, c4, audio0)
        c5 = self.add_clip("c5", 0, [audio1], 14, 6, 30, 38)
        self.register_auto_transition(c4, c5, audio1)
        c6 = self.add_clip("c6", 1, [audio1, video], 10, 5, 7)
        c7 = self.add_clip("c7", 1, [audio0, video], 15, 5, 1, 15)
        g0 = self.add_group("g0", [c3, c4, c5, c6, c7])

        c8 = self.add_clip("c8", 1, [audio0], 0, 7, 3, 13)
        c9 = self.add_clip("c9", 1, [audio1], 3, 7)
        g1 = self.add_group("g1", [c8, c9])
        c10 = self.add_clip("c10", 1, [audio0], 20, 7, 1)
        c11 = self.add_clip("c11", 1, [audio1], 23, 7, 3, 10)
        g2 = self.add_group("g2", [c10, c11])

        c12 = self.add_clip("c12", 1, [audio0], 3, 9)
        self.register_auto_transition(c8, c12, audio0)
        g3 = self.add_group("g3", [g1, c12])
        c13 = self.add_clip("c13", 1, [audio1], 18, 9)
        self.register_auto_transition(c13, c11, audio1)
        g4 = self.add_group("g4", [g2, c13])

        self.assertTimelineConfig()

        # edit failures
        self.timeline.set_snapping_distance(2)

        # cannot roll c10 to 22, which snaps to 23, because it will
        # extend c5 beyond its duration limit of 8
        self.assertFailEdit(
            c10, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 22,
            GES.Error.NOT_ENOUGH_INTERNAL_CONTENT)

        # same with g2
        self.assertFailEdit(
            g2, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 22,
            GES.Error.NOT_ENOUGH_INTERNAL_CONTENT)

        # cannot roll end c9 to 8, which snaps to 7, because it would
        # cause c3's in-point to become negative
        self.assertFailEdit(
            c9, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 8,
            GES.Error.NEGATIVE_TIME)

        # same with g1
        self.assertFailEdit(
            g1, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 8,
            GES.Error.NEGATIVE_TIME)

        # cannot roll c13 to 19, snap to 20, because it would cause
        # c4 to fully overlap c5
        self.assertFailEdit(
            c13, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 19,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # cannot roll c12 to 11, snap to 10, because it would cause
        # c3 to fully overlap c4
        self.assertFailEdit(
            c12, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 11,
            GES.Error.INVALID_OVERLAP_IN_TRACK)

        # give c6 a bit more allowed duration so we can focus on c9
        self.assertTrue(c6.set_inpoint(10))
        self.assertTimelineConfig({ c6 : {"in-point": 10}})
        # cannot roll c6 to 0 because it would cause c9 to be trimmed
        # below its start
        self.assertFailEdit(
            c6, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 0,
            GES.Error.NEGATIVE_TIME)
        # set back
        self.assertTrue(c6.set_inpoint(7))
        self.assertTimelineConfig({ c6 : {"in-point": 7}})

        # give c7 a bit more allowed duration so we can focus on c10
        self.assertTrue(c7.set_inpoint(0))
        self.assertTimelineConfig({ c7 : {"in-point": 0}})
        # cannot roll end c7 to 30 because it would cause c10 to be
        # trimmed beyond its end
        self.assertFailEdit(
            c7, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 30,
            GES.Error.NEGATIVE_TIME)
        # set back
        self.assertTrue(c7.set_inpoint(1))
        self.assertTimelineConfig({ c7 : {"in-point": 1}})

        # moving layer is not supported
        self.assertFailEdit(
            c0, 2, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 7, None)
        self.assertFailEdit(
            c0, 2, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 23, None)

        # successes
        self.timeline.set_snapping_distance(0)

        # c1 and g1 are trimmed at their end
        # NOTE: c12 is not trimmed even though it shares a group g3
        # with g1 because g3 does not share the same edge
        # trim forward
        self.assertEdit(
            c6, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 11, None,
            [], [],
            {
                c6 : {"start": 11, "in-point": 8, "duration": 4},
                c1 : {"duration": 11},
                c9 : {"duration": 8},
                g1 : {"duration": 11},
            }, [], [])
        # and reset
        self.assertEdit(
            c6, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 10, None,
            [], [],
            {
                c6 : {"start": 10, "in-point": 7, "duration": 5},
                c1 : {"duration": 10},
                c9 : {"duration": 7},
                g1 : {"duration": 10},
            }, [], [])

        # same with g0
        self.assertEdit(
            g0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 11, None,
            [], [],
            {
                c6 : {"start": 11, "in-point": 8, "duration": 4},
                c3 : {"start": 11, "in-point": 3, "duration": 5},
                g0 : {"start": 11, "duration": 9},
                c1 : {"duration": 11},
                c9 : {"duration": 8},
                g1 : {"duration": 11},
            }, [], [])
        self.assertEdit(
            g0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 10, None,
            [], [],
            {
                c6 : {"start": 10, "in-point": 7, "duration": 5},
                c3 : {"start": 10, "in-point": 2, "duration": 6},
                g0 : {"start": 10, "duration": 10},
                c1 : {"duration": 10},
                c9 : {"duration": 7},
                g1 : {"duration": 10},
            }, [], [])

        self.timeline.set_snapping_distance(1)
        # trim backward
        # NOTE: c9 has zero width, not considered overlapping with c6
        # snapping allows the edit to succeed (in-point of c6 no longer
        # negative)
        # NOTE: c12 does not move, but c8 does because it is in the same
        # group as g1
        # loose transitions
        self.assertEdit(
            c6, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 2, 3,
            [c6], [c12],
            {
                c6 : {"start": 3, "in-point": 0, "duration": 12},
                g0 : {"start": 3, "duration": 17},
                c1 : {"duration": 3},
                c8 : {"duration": 3},
                c9 : {"duration": 0},
                g1 : {"duration": 3},
            }, [], [(c1, c0, video), (c8, c12, audio0)])

        # bring back
        # NOTE: no snapping to c3 start edge because it is part of the
        # element being edited, g0, even though it doesn't end up changing
        # gain back new transitions
        self.assertEdit(
            g0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 10, None,
            [], [],
            {
                c6 : {"start": 10, "in-point": 7, "duration": 5},
                g0 : {"start": 10, "duration": 10},
                c1 : {"duration": 10},
                c8 : {"duration": 10},
                c9 : {"duration": 7},
                g1 : {"duration": 10},
            }, [(c1, c0, video), (c8, c12, audio0)], [])


        # same but with the end edge of g0
        self.timeline.set_snapping_distance(0)
        self.assertEdit(
            c7, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 19, None, [], [],
            {
                c7 : {"duration": 4},
                c2 : {"start": 19, "in-point": 19, "duration": 11},
                c10 : {"start": 19, "in-point": 0, "duration": 8},
                g2 : {"start": 19, "duration": 11},
            }, [], [])
        self.assertEdit(
            c7, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 20, None, [], [],
            {
                c7 : {"duration": 5},
                c2 : {"start": 20, "in-point": 20, "duration": 10},
                c10 : {"start": 20, "in-point": 1, "duration": 7},
                g2 : {"start": 20, "duration": 10},
            }, [], [])
        # do same with g0
        self.assertEdit(
            g0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 19, None, [], [],
            {
                c7 : {"duration": 4},
                c5 : {"duration": 5},
                g0 : {"duration": 9},
                c2 : {"start": 19, "in-point": 19, "duration": 11},
                c10 : {"start": 19, "in-point": 0, "duration": 8},
                g2 : {"start": 19, "duration": 11},
            }, [], [])
        self.assertEdit(
            g0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 20, None, [], [],
            {
                c7 : {"duration": 5},
                c5 : {"duration": 6},
                g0 : {"duration": 10},
                c2 : {"start": 20, "in-point": 20, "duration": 10},
                c10 : {"start": 20, "in-point": 1, "duration": 7},
                g2 : {"start": 20, "duration": 10},
            }, [], [])

        self.timeline.set_snapping_distance(1)
        # trim forwards
        # NOTE: c10 has zero width, not considered overlapping with c7
        # snapping allows the edit to succeed (duration of c7 no longer
        # above its limit)
        # NOTE: c12 does not move, but c11 does because it is in the same
        # group as g2
        self.assertEdit(
            c7, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 28, 27,
            [c7], [c13],
            {
                c7 : {"duration": 12},
                g0 : {"duration": 17},
                c2 : {"start": 27, "in-point": 27, "duration": 3},
                c10 : {"start": 27, "in-point": 8, "duration": 0},
                c11 : {"start": 27, "in-point": 7, "duration": 3},
                g2 : {"start": 27, "duration": 3},
            }, [], [(c0, c2, video), (c13, c11, audio1)])
        # bring back using g0
        # NOTE: no snapping to c5 end edge because it is part of the
        # element being edited, g0, even though it doesn't end up changing
        self.assertEdit(
            g0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 20, None, [], [],
            {
                c7 : {"duration": 5},
                g0 : {"duration": 10},
                c2 : {"start": 20, "in-point": 20, "duration": 10},
                c10 : {"start": 20, "in-point": 1, "duration": 7},
                c11 : {"start": 20, "in-point": 0, "duration": 10},
                g2 : {"start": 20, "duration": 10},
            }, [(c0, c2, video), (c13, c11, audio1)], [])

        # adjust c0 for snapping
        # doesn't move anything else
        self.assertEdit(
            c0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 8, None,
            [], [],
            {
                c0 : {"start": 8, "in-point": 1, "duration": 15},
            }, [], [])
        self.assertEdit(
            c0, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 22, None, [], [],
            {
                c0 : {"duration": 14},
            }, [], [])
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                        *===========================*                  ]
                        |             c0            |                  ]
        *===============*===*===================*===*===============*  ]>video
        |        c1         |                   |         c2        |  ]
        *===================*                   *===================*  ]
                            ..........g0.........
                            *===========*       :                      ]
                            |     c3    |       :                      ]>audio0
                            *===*=======*===*   :                      ]
                            :   |    c4     |   :                      ] ]
                            :   *===*=======*===*                        ]
                            :       |    c5     |                        ]>audio1
                            :       *===========*                        ]
        ____________________:___________________:____________________
        layer1______________:___________________:____________________
                            :                   :
        .............g3.....:....               :
        :     *=================*               :                      ]
        :     |      c12        |           ....:...g4...............  ]
        :     *=================*           :   :                   :  ]
        :........g1.........:   :           :   :.........g2........:  ]>audio0
        *===================*   :           :   *=============*     :  ]
        |        c8         |   :           :   |      c10    |     :  ]
        *===================*   :     *=========*=============*     :  ]
        :                   :   :     |   c7    |                   :  ] ]
        :                   *=========*=========*                   :    ]>video
        :                   |    c6   |     :   :                   :  ] ]
        :     *=============*=========*     :   *===================*  ]
        :     |     c9      |...:...........:...|        c11        |  ]
        :     *=============*   :           :   *===================*  ]
        :...................:   :           :   :...................:  ]>audio1
        :.......................:           *=================*     :  ]
                                            |        c13      |     :  ]
                                            *=================*     :  ]
                                            :.......................:
        """
        # rolling only moves an element if it contains a source that
        # touches the rolling edge. For a group, any source below it
        # at the corresponding edge counts, we also prefer trimming the
        # whole group over just one of its childrens.
        # As such, when rolling the end of c5, c11 shares the audio1
        # track and starts when c5 ends, so is set to be trimmed. But it
        # is also at the start edge of its parent g2, so g2 is set to be
        # trimmed. However, it is not at the start of g4, so g4 is not
        # set to be trimmed. As such, c10 will also move, even though it
        # does not share a track. c2, on the other hand, will not move
        # NOTE: snapping helps keep c5's duration below its limit (8)
        self.assertEdit(
            c5, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 23, 22,
            [c5], [c0],
            {
                c5 : {"duration": 8},
                g0 : {"duration": 12},
                c11 : {"start": 22, "in-point": 2, "duration": 8},
                c10 : {"start": 22, "in-point": 3, "duration": 5},
                g2 : {"start": 22, "duration": 8},
            }, [], [])

        # same with c3 at its start edge
        self.assertEdit(
            c3, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 7, 8,
            [c3], [c0],
            {
                c3 : {"start": 8, "in-point": 0, "duration": 8},
                g0 : {"start": 8, "duration": 14},
                c8 : {"duration": 8},
                c9 : {"duration": 5},
                g1 : {"duration": 8},
            }, [], [])
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                        *===========================*                  ]
                        |             c0            |                  ]
        *===============*===*===================*===*===============*  ]>video
        |        c1         |                   |         c2        |  ]
        *===================*                   *===================*  ]
                        ..............g0.............
                        *===============*           :                  ]
                        |      c3       |           :                  ]>audio0
                        *=======*=======*===*       :                  ]
                        :       |    c4     |       :                  ] ]
                        :       *===*=======*=======*                    ]
                        :           |      c5       |                    ]>audio1
                        :           *===============*                    ]
        ________________:___________________________:________________
        layer1__________:___________________________:________________
                        :                           :
        .............g3.:........                   :
        :     *=================*                   :                  ]
        :     |      c12        |           ........:.g4.............  ]
        :     *=================*           :       :               :  ]
        :........g1.....:       :           :       :.....g2........:  ]>audio0
        *===============*       :           :       *=========*     :  ]
        |      c8       |       :           :       |   c10   |     :  ]
        *===============*       :     *=========*   *=========*     :  ]
        :               :       :     |   c7    |   :               :  ] ]
        :               :   *=========*=========*   :               :    ]>video
        :               :   |    c6   |     :       :               :  ] ]
        :     *=========*   *=========*     :       *===============*  ]
        :     |   c9    |.......:...........:.......|     c11       |  ]
        :     *=========*       :           :       *===============*  ]
        :...............:       :           :       :...............:  ]>audio1
        :.......................:           *=================*     :  ]
                                            |        c13      |     :  ]
                                            *=================*     :  ]
                                            :.......................:
        """
        # rolling end of c1 only moves c6, similarly with c2 and c7
        self.assertEdit(
            c1, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 8, 8,
            [c1], [c9, c8, c3, c0],
            {
                c1 : {"duration": 8},
                c6 : {"start": 8, "in-point": 5, "duration": 7},
            }, [], [(c1, c0, video)])
        self.assertEdit(
            c2, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 22, 22,
            [c2], [c0, c5, c10, c11],
            {
                c2 : {"start": 22, "in-point": 22, "duration": 8},
                c7 : {"duration": 7},
            }, [], [(c0, c2, video)])

        # move c3 end edge out the way
        self.timeline.set_snapping_distance(0)
        self.assertEdit(
            c3, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 17, None, [], [],
            {
                c3: {"duration": 9},
            }, [], [])

        self.timeline.set_snapping_distance(2)

        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                        *===========================*                  ]
                        |             c0            |                  ]
        *===============*===========================*===============*  ]>video
        |       c1      |                           |      c2       |  ]
        *===============*                           *===============*  ]
                        ..............g0.............
                        *=================*         :                  ]
                        |      c3         |         :                  ]>audio0
                        *=======*=========*=*       :                  ]
                        :       |    c4     |       :                  ] ]
                        :       *===*=======*=======*                    ]
                        :           |      c5       |                    ]>audio1
                        :           *===============*                    ]
        ________________:___________________________:________________
        layer1__________:___________________________:________________
                        :                           :
        .............g3.:........                   :
        :     *=================*                   :                  ]
        :     |      c12        |           ........:.g4.............  ]
        :     *=================*           :       :               :  ]
        :........g1.....:       :           :       :.....g2........:  ]>audio0
        *===============*       :           :       *=========*     :  ]
        |      c8       |       :           :       |   c10   |     :  ]
        *===============*       :     *=============*=========*     :  ]
        :               :       :     |     c7      |               :  ] ]
        :               *=============*=============*               :    ]>video
        :               |      c6     |     :       :               :  ] ]
        :     *=========*=============*     :       *===============*  ]
        :     |   c9    |.......:...........:.......|     c11       |  ]
        :     *=========*       :           :       *===============*  ]
        :...............:       :           :       :...............:  ]>audio1
        :.......................:           *=================*     :  ]
                                            |        c13      |     :  ]
                                            *=================*     :  ]
                                            :.......................:
        """

        # can safely roll within a group
        # NOTE: we do not snap to an edge used in the edit
        self.assertEdit(
            c6, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_END, 15, 14,
            [c6], [c5],
            {
                c6: {"duration": 6},
                c7: {"start": 14, "in-point": 0, "duration": 8},
            }, [], [])
        self.assertEdit(
            c7, -1, GES.EditMode.EDIT_ROLL, GES.Edge.EDGE_START, 16, 17,
            [c7], [c3],
            {
                c6: {"duration": 9},
                c7: {"start": 17, "in-point": 3, "duration": 5},
            }, [], [])

    def test_snap_from_negative(self):
        track = self.add_video_track()
        c0 = self.add_clip("c0", 0, [track], 0, 20)
        c1 = self.add_clip("c1", 0, [track], 100, 10)
        g1 = self.add_group("g0", [c0, c1])
        snap_to = self.add_clip("snap-to", 2, [track], 4, 50)

        self.assertTimelineConfig()

        self.timeline.set_snapping_distance(9)
        # move without snap would make start edge of c0 go to -5, but this
        # edge snaps to the start edge of snap_to, allowing the edit to
        # succeed
        self.assertEdit(
            c1, 1, GES.EditMode.NORMAL, GES.Edge.NONE, 95, 4, [c0], [snap_to],
            {
                c0 : {"start": 4, "layer": 1},
                c1 : {"start": 104, "layer": 1},
                g1 : {"start": 4, "layer": 1},
            }, [], [])

    def test_move_layer(self):
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                                      *===================*
                                      |         c1        |
                                      *===================*
                  ..............g2...............
                  :                             :
                  :.............g0..............:
                  *=============================*
                  |             c0              |
                  *=============================*
                  :                             :
                  :                             :
        __________:_____________________________:____________________
        layer1____:_____________________________:____________________
                  :                             :
                  *===================*         :
                  |        c2         |         :
                  *===================*         :
                  :.............................:
                  :         ...............g1...:..........
                  :         *=============================*
                  :         |              c3             |
                  :         *=============================*
        __________:_________:_____________________________:__________
        layer2____:_________:_____________________________:__________
                  :         :                             :
                  :         :         *===================*
                  :         :         |        c5         |
                  :         :.........*===================*
                  *===================*         :
                  |         c4        |         :
                  *===================*         :
                  :.............................:
                            *===================*
                            |         c6        |
                            *===================*
        """
        track = self.add_video_track()
        c0 = self.add_clip("c0", 0, [track], 5, 15)
        c1 = self.add_clip("c1", 0, [track], 15, 10)
        self.register_auto_transition(c0, c1, track)
        c2 = self.add_clip("c2", 1, [track], 5, 10)
        c3 = self.add_clip("c3", 1, [track], 10, 15)
        self.register_auto_transition(c2, c3, track)
        c4 = self.add_clip("c4", 2, [track], 5, 10)
        c5 = self.add_clip("c5", 2, [track], 15, 10)
        c6 = self.add_clip("c6", 2, [track], 10, 10)
        self.register_auto_transition(c4, c6, track)
        self.register_auto_transition(c6, c5, track)

        g0 = self.add_group("g0", [c0, c2])
        g1 = self.add_group("g1", [c3, c5])
        g2 = self.add_group("g2", [g0, c4])

        self.assertTimelineConfig()

        layer = self.timeline.get_layer(0)
        self.assertIsNotNone(layer)

        # don't loose auto-transitions
        # clips stay in their layer (groups do not move them)
        self.timeline.move_layer(layer, 2)
        self.assertTimelineConfig(
            {
                c0 : {"layer": 2},
                c1 : {"layer": 2},
                c2 : {"layer": 0},
                c3 : {"layer": 0},
                c4 : {"layer": 1},
                c5 : {"layer": 1},
                c6 : {"layer": 1},
                g1 : {"layer": 0},
            })
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                  ..............g2...............
                  :                             :
                  :.............g0..............:
                  *===================*         :
                  |        c2         |         :
                  *===================*         :
                  :   ..........................:
                  :   :     ...............g1..............
                  :   :     *=============================*
                  :   :     |              c3             |
                  :   :     *=============================*
        __________:___:_____:_____________________________:__________
        layer1____:___:_____:_____________________________:__________
                  :   :     :                             :
                  :   :     :         *===================*
                  :   :     :         |        c5         |
                  :   :     :         *===================*
                  :   :     :.............................:
                  :   :..........g2.....
                  :   g0               :
                  :   :                :
                  *===================*:
                  |         c4        |:
                  *===================*:
                  :   :................:
                  :   :     *===================*
                  :   :     |         c6        |
                  :   :     *===================*
        __________:___:______________________________________________
        layer2____:___:______________________________________________
                  :   :..........................
                  *=============================*
                  |             c0              |
                  *=============================*
                  :.............................:
                  :.............................:
                                      *===================*
                                      |         c1        |
                                      *===================*
        """
        layer = self.timeline.get_layer(1)
        self.assertIsNotNone(layer)
        self.timeline.move_layer(layer, 0)
        self.assertTimelineConfig(
            {
                c2 : {"layer": 1},
                c3 : {"layer": 1},
                c4 : {"layer": 0},
                c5 : {"layer": 0},
                c6 : {"layer": 0},
                g0 : {"layer": 1},
            })
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                            *===================*
                            |         c6        |
                            *===================*

                  ..............g2...............
                  *===================*         :
                  |         c4        |         :
                  *===================*         :
                  :         ...............g1...:..........
                  :         :         *===================*
                  :         :         |        c5         |
                  :         :         *===================*
        __________:_________:___________________:_________:__________
        layer1____:_________:___________________:_________:__________
                  :         :                   :         :
                  :         *=============================*
                  :         |              c3             |
                  :         *=============================*
                  :         :...................:.........:
                  :.............g0..............:
                  *===================*         :
                  |        c2         |         :
                  *===================*         :
                  :.............................:
        __________:_____________________________:____________________
        layer2____:_____________________________:____________________
                  :                             :
                  *=============================*
                  |             c0              |
                  *=============================*
                  :.............................:
                  :.............................:
                                      *===================*
                                      |         c1        |
                                      *===================*
        """
        self.timeline.append_layer()
        layer = self.timeline.get_layer(3)
        self.assertIsNotNone(layer)
        self.timeline.move_layer(layer, 1)
        self.assertTimelineConfig(
            {
                c0 : {"layer": 3},
                c1 : {"layer": 3},
                c2 : {"layer": 2},
                c3 : {"layer": 2},
                g0 : {"layer": 2},
            })
        layer = self.timeline.get_layer(3)
        self.assertIsNotNone(layer)
        self.timeline.move_layer(layer, 0)
        self.assertTimelineConfig(
            {
                c0 : {"layer": 0},
                c1 : {"layer": 0},
                c2 : {"layer": 3},
                c3 : {"layer": 3},
                c4 : {"layer": 1},
                c5 : {"layer": 1},
                c6 : {"layer": 1},
                g0 : {"layer": 0},
                g1 : {"layer": 1},
            })
        """
        , . . . . , . . . . , . . . . , . . . . , . . . . , . . . . ,
        0         5         10        15        20        25        30
        _____________________________________________________________
        layer0_______________________________________________________

                                      *===================*
                                      |         c1        |
                                      *===================*
                  ..............g2...............
                  :.............g0..............:
                  *=============================*
                  |             c0              |
                  *=============================*
                  :   ..........................:
        __________:___:______________________________________________
        layer1____:___:______________________________________________
                  :   :
                  :   :.......g2.......
                  :   g0              :
                  :   :               :
                  *===================*
                  |         c4        |
                  *===================*
                  :   :...............:
                  :   :     *===================*
                  :   :     |         c6        |
                  :   :     *===================*
                  :   :     ...............g1..............
                  :   :     :         *===================*
                  :   :     :         |        c5         |
                  :   :     :         *===================*
        __________:___:_____:_____________________________:__________
        layer2____:___:_____:_____________________________:__________
        __________:___:_____:_____________________________:__________
        layer3____:___:_____:_____________________________:__________
                  :   :     :                             :
                  :   :     *=============================*
                  :   :     |              c3             |
                  :   :     *=============================*
                  :   :     :.............................:
                  :   :..........................
                  *===================*         :
                  |        c2         |         :
                  *===================*         :
                  :.............................:
                  :.............................:
        """
        layer = self.timeline.get_layer(1)
        self.assertTrue(self.timeline.remove_layer(layer))

        # TODO: add tests when removing layers:
        # FIXME: groups should probably loose their children when they
        # are removed from the timeline, which would change g1's
        # priority, but currently c5 remains in the group with priority
        # of the removed layer

    def test_not_snappable(self):
        track = self.add_video_track()
        c0 = self.add_clip("c0", 0, [track], 0, 10)
        no_source = self.add_clip(
            "no-source", 0, [], 5, 10, effects=[GES.Effect.new("agingtv")])
        effect_clip = self.add_clip(
            "effect-clip", 0, [track], 5, 10, clip_type=GES.EffectClip,
            asset_id="agingtv || audioecho")
        text = self.add_clip(
            "text-clip", 0, [track], 5, 10, clip_type=GES.TextOverlayClip)

        self.assertTimelineConfig()

        self.timeline.set_snapping_distance(20)

        self.assertEdit(
            c0, 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 8, None,
            [], [], {c0 : {"start": 8}}, [], [])
        self.assertEdit(
            c0, 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_START, 5, None,
            [], [], {c0 : {"start": 5}}, [], [])
        self.assertEdit(
            c0, 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_END, 8, None,
            [], [], {c0 : {"duration": 3}}, [], [])

        c1 = self.add_clip("c1", 0, [track], 30, 3)
        self.assertTimelineConfig()

        # end edge snaps to start of c1
        self.assertEdit(
            c0, 0, GES.EditMode.EDIT_NORMAL, GES.Edge.EDGE_NONE, 10, 30,
            [c0], [c1], {c0 : {"start": 27}}, [], [])

    def test_disable_timeline_editing_apis(self):
        track = self.add_video_track()
        self.assertEqual(self.timeline.props.auto_transition, True)
        self.timeline.disable_edit_apis(True)
        self.assertEqual(self.timeline.props.auto_transition, False)

        c0 = self.add_clip("c0", 0, [track], 0, 10)
        # Without disabling edit API adding clip would fail
        c1 = self.add_clip("c1", 0, [track], 0, 10)
        self.assertTimelineConfig()

        c1.set_start(1)
        c1.set_duration(1)
        self.assertEqual(c1.get_start(), 1)
        self.assertEqual(c1.get_duration(), 1)


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
            def loaded_cb(unused_project, unused_timeline):
                mainloop.quit()
            project.connect("loaded", loaded_cb)

            mainloop.run()

            layers = timeline.get_layers()
            self.assertEqual(len(layers), 1)

            self.assertTrue(layers[0].props.auto_transition)

    def test_transition_type(self):
        xges = self.create_xges()
        with common.created_project_file(xges) as proj_uri:
            project = GES.Project.new(proj_uri)
            timeline = project.extract()

            mainloop = common.create_main_loop()
            def loaded_cb(unused_project, unused_timeline):
                mainloop.quit()
            project.connect("loaded", loaded_cb)
            mainloop.run()

            layers = timeline.get_layers()
            self.assertEqual(len(layers), 1)

            clips = layers[0].get_clips()
            clip1 = clips[0]
            clip2 = clips[-1]
            # There should be a transition because clip1 intersects clip2
            self.assertLess(clip1.props.start, clip2.props.start)
            self.assertLess(clip2.props.start, clip1.props.start + clip1.props.duration)
            self.assertLess(clip1.props.start + clip1.props.duration, clip2.props.start + clip2.props.duration)
            self.assertEqual(len(clips), 3)


class TestPriorities(common.GESSimpleTimelineTest):

    def test_clips_priorities(self):
        clip = self.add_clip(0, 0, 100)
        clip1 = self.add_clip(100, 0, 100)
        self.timeline.commit()

        self.assertLess(clip.props.priority, clip1.props.priority)

        clip.props.start = 101
        self.timeline.commit()
        self.assertGreater(clip.props.priority, clip1.props.priority)


class TestTimelineElement(common.GESSimpleTimelineTest):

    def test_set_child_property(self):
        clip = self.add_clip(0, 0, 100)
        source = clip.find_track_element(None, GES.VideoSource)
        self.assertTrue(source.set_child_property("height", 5))
        self.assertEqual(clip.get_child_property("height"), (True, 5))
