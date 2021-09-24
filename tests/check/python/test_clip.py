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

import os
import tempfile

import gi
gi.require_version("Gst", "1.0")
gi.require_version("GES", "1.0")

from gi.repository import Gst  # noqa
Gst.init(None)  # noqa
from gi.repository import GES  # noqa
GES.init()

from . import common  # noqa

import unittest  # noqa


class TestCopyPaste(unittest.TestCase):

    def setUp(self):
        self.timeline = GES.Timeline.new_audio_video()
        self.assertEqual(len(self.timeline.get_tracks()), 2)
        self.layer = self.timeline.append_layer()

    def testCopyClipRemoveAndPaste(self):
        clip1 = GES.TestClip.new()
        clip1.props.duration = 10

        self.layer.add_clip(clip1)

        self.assertEqual(len(clip1.get_children(False)), 2)

        copy = clip1.copy(True)
        self.assertEqual(len(self.layer.get_clips()), 1)

        self.layer.remove_clip(clip1)

        copy.paste(10)
        self.assertEqual(len(self.layer.get_clips()), 1)

    def testCopyPasteTitleClip(self):
        clip1 = GES.TitleClip.new()
        clip1.props.duration = 10

        self.layer.add_clip(clip1)
        self.assertEqual(len(clip1.get_children(False)), 1)

        copy = clip1.copy(True)
        self.assertEqual(len(self.layer.get_clips()), 1)

        copy.paste(10)
        self.assertEqual(len(self.layer.get_clips()), 2)


class TestTransitionClip(unittest.TestCase):

    def test_serialize_invert(self):
        timeline = GES.Timeline.new()
        timeline.add_track(GES.VideoTrack.new())
        layer = timeline.append_layer()

        clip1 = GES.TransitionClip.new_for_nick("crossfade")
        clip1.props.duration = Gst.SECOND
        self.assertTrue(layer.add_clip(clip1))

        vtransition, = clip1.children
        vtransition.set_inverted(True)
        self.assertEqual(vtransition.props.invert, True)

        with tempfile.NamedTemporaryFile() as tmpxges:
            uri = Gst.filename_to_uri(tmpxges.name)
            timeline.save_to_uri(uri, None, True)

            timeline = GES.Timeline.new_from_uri(uri)
            project = timeline.get_asset()
            mainloop = common.create_main_loop()
            project.connect("loaded", lambda _, __: mainloop.quit())
            mainloop.run()
            self.assertIsNotNone(timeline)
            layer, = timeline.get_layers()
            clip, = layer.get_clips()
            vtransition, = clip.children
            self.assertEqual(vtransition.props.invert, True)

class TestTitleClip(unittest.TestCase):

    def testSetColor(self):
        timeline = GES.Timeline.new_audio_video()
        clip = GES.TitleClip.new()
        timeline.append_layer().add_clip(clip )
        self.assertTrue(clip.set_child_property('color', 1))
        self.assertTrue(clip.set_child_property('color', 4294967295))

    def testGetPropertyNotInTrack(self):
        title_clip = GES.TitleClip.new()
        self.assertEqual(title_clip.props.text, "")
        self.assertEqual(title_clip.props.font_desc, "Serif 36")

    def test_split_effect(self):
        timeline = GES.Timeline.new()
        timeline.add_track(GES.VideoTrack.new())
        layer = timeline.append_layer()

        clip1 = GES.TitleClip.new()
        clip1.props.duration = Gst.SECOND
        self.assertTrue(layer.add_clip(clip1))

        effect = GES.Effect.new("agingtv")
        self.assertTrue(clip1.add(effect))

        children1 = clip1.get_children(True)
        self.assertNotEqual(children1[0].props.priority,
                            children1[1].props.priority)

        clip2 = clip1.split(Gst.SECOND / 2)

        children1 = clip1.get_children(True)
        self.assertNotEqual(children1[0].props.priority,
                            children1[1].props.priority)

        children2 = clip2.get_children(True)
        self.assertNotEqual(children2[0].props.priority,
                            children2[1].props.priority)


class TestUriClip(common.GESSimpleTimelineTest):
    def test_max_duration_on_extract(self):
        asset = GES.UriClipAsset.request_sync(common.get_asset_uri("audio_video.ogg"))
        clip = asset.extract()

        self.assertEqual(clip.props.max_duration, Gst.SECOND)


class TestTrackElements(common.GESSimpleTimelineTest):

    def test_add_to_layer_with_effect_remove_add(self):
        timeline = GES.Timeline.new_audio_video()
        video_track, audio_track = timeline.get_tracks()
        layer = timeline.append_layer()

        test_clip = GES.TestClip()
        self.assertEqual(test_clip.get_children(True), [])
        self.assertTrue(layer.add_clip(test_clip))
        audio_source = test_clip.find_track_element(None, GES.AudioSource)
        video_source = test_clip.find_track_element(None, GES.VideoSource)

        self.assertTrue(test_clip.set_child_property("volume", 0.0))
        self.assertEqual(audio_source.get_child_property("volume")[1], 0.0)

        effect = GES.Effect.new("agingtv")
        test_clip.add(effect)
        self.assertEqual(audio_source.props.track, audio_track)
        self.assertEqual(video_source.props.track, video_track)
        self.assertEqual(effect.props.track, video_track)

        children = test_clip.get_children(True)
        layer.remove_clip(test_clip)
        self.assertEqual(test_clip.get_children(True), children)
        self.assertEqual(audio_source.props.track, None)
        self.assertEqual(video_source.props.track, None)
        self.assertEqual(effect.props.track, None)

        self.assertTrue(layer.add_clip(test_clip))
        self.assertEqual(test_clip.get_children(True), children)
        self.assertEqual(audio_source.props.track, audio_track)
        self.assertEqual(video_source.props.track, video_track)
        self.assertEqual(effect.props.track, video_track)

        audio_source = test_clip.find_track_element(None, GES.AudioSource)
        self.assertFalse(audio_source is None)
        self.assertEqual(audio_source.get_child_property("volume")[1], 0.0)
        self.assertEqual(audio_source.props.track, audio_track)
        self.assertEqual(video_source.props.track, video_track)
        self.assertEqual(effect.props.track, video_track)

    def test_effects_priority(self):
        timeline = GES.Timeline.new_audio_video()
        layer = timeline.append_layer()

        test_clip = GES.TestClip.new()
        layer.add_clip(test_clip)
        self.assert_effects(test_clip)

        effect1 = GES.Effect.new("agingtv")
        test_clip.add(effect1)
        self.assert_effects(test_clip, effect1)

        test_clip.set_top_effect_index(effect1, 1)
        self.assert_effects(test_clip, effect1)
        test_clip.set_top_effect_index(effect1, 10)
        self.assert_effects(test_clip, effect1)

        effect2 = GES.Effect.new("dicetv")
        test_clip.add(effect2)
        self.assert_effects(test_clip, effect1, effect2)

        test_clip.remove(effect1)
        self.assert_effects(test_clip, effect2)

    def test_effects_index(self):
        timeline = GES.Timeline.new_audio_video()
        layer = timeline.append_layer()

        test_clip = GES.TestClip.new()
        layer.add_clip(test_clip)
        self.assert_effects(test_clip)

        ref_effects_list = []

        def add_effect(effect):
            test_clip.add(effect)
            ref_effects_list.append(effect)

            self.assert_effects(test_clip, *ref_effects_list)

        def move_effect(old_index, new_index):
            effect = ref_effects_list[old_index]
            self.assertTrue(test_clip.set_top_effect_index(effect, new_index))

            ref_effects_list.insert(new_index, ref_effects_list.pop(old_index))

            self.assert_effects(test_clip, *ref_effects_list)

        effects = ["agingtv", "dicetv", "burn", "gamma", "edgetv", "alpha",
            "exclusion", "chromahold", "coloreffects", "videobalance"]

        for effect in effects:
            add_effect(GES.Effect.new(effect))

        move_effect(3, 8)
        move_effect(5, 6)
        move_effect(0, 9)

        self.assert_effects(test_clip, *ref_effects_list)

    def test_signal_order_when_removing_effect(self):
        timeline = GES.Timeline.new_audio_video()
        layer = timeline.append_layer()

        test_clip = GES.TestClip.new()
        layer.add_clip(test_clip)
        self.assert_effects(test_clip)

        effect1 = GES.Effect.new("agingtv")
        test_clip.add(effect1)
        effect2 = GES.Effect.new("dicetv")
        test_clip.add(effect2)
        self.assert_effects(test_clip, effect1, effect2)

        mainloop = common.create_main_loop()

        signals = []

        def handler_cb(*args):
            signals.append(args[-1])

        test_clip.connect("child-removed", handler_cb, "child-removed")
        effect2.connect("notify::priority", handler_cb, "notify::priority")
        test_clip.remove(effect1)
        test_clip.disconnect_by_func(handler_cb)
        effect2.disconnect_by_func(handler_cb)
        self.assert_effects(test_clip, effect2)

        mainloop.run(until_empty=True)

        self.assertEqual(signals, ["child-removed", "notify::priority"])

    def test_moving_core_track_elements(self):
        clip = self.append_clip()
        clip1 = self.append_clip()
        title_clip = self.append_clip(asset_type=GES.TitleClip)

        track_element = clip.find_track_element(None, GES.VideoSource)
        self.assertTrue(clip.remove(track_element))

        track_element1 = clip1.find_track_element(None, GES.VideoSource)
        self.assertTrue(clip1.remove(track_element1))

        self.assertTrue(clip1.add(track_element))
        self.assertIsNotNone(track_element.get_track())
        # We can add another TestSource to the clip as it has the same parent
        # asset
        self.assertTrue(clip1.add(track_element1))
        # We already have a core TrackElement for the video track, not adding
        # a second one.
        self.assertIsNone(track_element1.get_track())

        clip1.remove(track_element)
        clip1.remove(track_element1)
        title = title_clip.find_track_element(None, GES.VideoSource)
        self.assertTrue(title_clip.remove(title))
        # But we can't add an element that has been created by a TitleClip
        self.assertFalse(clip.add(title))
        self.assertFalse(title_clip.add(track_element))
        self.assertTrue(clip.add(track_element))
        self.assertTrue(clip1.add(track_element1))

    def test_ungroup_regroup(self):
        clip = self.append_clip()
        children = clip.get_children(True)

        clip1, clip2 = GES.Container.ungroup(clip, True)

        self.assertEqual(clip, clip1)
        clip1_child, = clip1.get_children(True)
        clip2_child, = clip2.get_children(True)
        self.assertCountEqual (children, [clip1_child, clip2_child])

        # can freely move children between the ungrouped clips
        self.assertTrue(clip1.remove(clip1_child))
        self.assertTrue(clip2.add(clip1_child))

        self.assertTrue(clip2.remove(clip2_child))
        self.assertTrue(clip1.add(clip2_child))

        grouped = GES.Container.group([clip1, clip2])
        self.assertEqual(grouped, clip1)

        self.assertCountEqual(clip1.get_children(True),
                [clip1_child, clip2_child])
        self.assertEqual(clip2.get_children(True), [])

        # can freely move children between the grouped clips
        self.assertTrue(clip1.remove(clip2_child))
        self.assertTrue(clip2.add(clip2_child))

        self.assertTrue(clip1.remove(clip1_child))
        self.assertTrue(clip2.add(clip1_child))

        self.assertTrue(clip2.remove(clip1_child))
        self.assertTrue(clip1.add(clip1_child))

        self.assertTrue(clip2.remove(clip2_child))
        self.assertTrue(clip1.add(clip2_child))

        # clip2 no longer part of the timeline
        self.assertIsNone(clip2.props.layer)
        self.assertEqual(clip1.props.layer, self.layer)
        self.assertIsNone(clip2.props.timeline)
        self.assertEqual(clip1.props.timeline, self.timeline)

    def test_image_source_asset(self):
        asset = GES.UriClipAsset.request_sync(common.get_asset_uri("png.png"))
        clip = self.layer.add_asset(asset, 0, 0, Gst.SECOND, GES.TrackType.UNKNOWN)

        image_src, = clip.get_children(True)

        self.assertTrue(image_src.get_asset().is_image())
        self.assertTrue(isinstance(image_src, GES.VideoUriSource))
        imagefreeze, = [e for e in image_src.get_nleobject().iterate_recurse()
            if e.get_factory().get_name() == "imagefreeze"]

        asset = GES.UriClipAsset.request_sync(common.get_asset_uri("audio_video.ogg"))
        clip = self.layer.add_asset(asset, Gst.SECOND, 0, Gst.SECOND, GES.TrackType.VIDEO)
        video_src, = clip.get_children(True)
        self.assertEqual([e for e in video_src.get_nleobject().iterate_recurse()
            if e.get_factory().get_name() == "imagefreeze"], [])
