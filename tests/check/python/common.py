# -*- coding: utf-8 -*-
#
# Copyright (c) 2016 Alexandru Băluț <alexandru.balut@gmail.com>
# Copyright (c) 2016, Thibault Saunier
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
from gi.repository import GLib  # noqa
from gi.repository import GObject  # noqa
import contextlib  # noqa
import os  #noqa
import unittest  # noqa
import tempfile  # noqa

Gst.init(None)
GES.init()


def create_main_loop():
    """Creates a MainLoop with a timeout."""
    mainloop = GLib.MainLoop()
    timed_out = False

    def timeout_cb(unused):
        nonlocal timed_out
        timed_out = True
        mainloop.quit()

    def run(timeout_seconds=5, until_empty=False):
        source = GLib.timeout_source_new_seconds(timeout_seconds)
        source.set_callback(timeout_cb)
        source.attach()
        if until_empty:
            GLib.idle_add(mainloop.quit)
        GLib.MainLoop.run(mainloop)
        source.destroy()
        if timed_out:
            raise Exception("Timed out after %s seconds" % timeout_seconds)

    mainloop.run = run
    return mainloop


def create_project(with_group=False, saved=False):
    """Creates a project with two clips in a group."""
    timeline = GES.Timeline.new_audio_video()
    layer = timeline.append_layer()

    if with_group:
        clip1 = GES.TitleClip()
        clip1.set_start(0)
        clip1.set_duration(10*Gst.SECOND)
        layer.add_clip(clip1)
        clip2 = GES.TitleClip()
        clip2.set_start(100 * Gst.SECOND)
        clip2.set_duration(10*Gst.SECOND)
        layer.add_clip(clip2)
        group = GES.Container.group([clip1, clip2])

    if saved:
        if isinstance(saved, str):
            suffix = "-%s.xges" % saved
        else:
            suffix = ".xges"
        uri = "file://%s" % tempfile.NamedTemporaryFile(suffix=suffix).name
        timeline.get_asset().save(timeline, uri, None, overwrite=True)

    return timeline


@contextlib.contextmanager
def created_project_file(xges):
    _, xges_path = tempfile.mkstemp(suffix=".xges")
    with open(xges_path, "w") as f:
        f.write(xges)

    yield Gst.filename_to_uri(os.path.abspath(xges_path))

    os.remove(xges_path)


def get_asset_uri(name):
    python_tests_dir = os.path.dirname(os.path.abspath(__file__))
    assets_dir = os.path.join(python_tests_dir, "..", "assets")
    return Gst.filename_to_uri(os.path.join(assets_dir, name))


class GESTest(unittest.TestCase):

    def _log(self, func, format, *args):
        string = format
        if args:
            string = string % args[0]
        func(string)

    def log(self, format, *args):
        self._log(Gst.log, format, *args)

    def debug(self, format, *args):
        self._log(Gst.debug, format, *args)

    def info(self, format, *args):
        self._log(Gst.info, format, *args)

    def fixme(self, format, *args):
        self._log(Gst.fixme, format, *args)

    def warning(self, format, *args):
        self._log(Gst.warning, format, *args)

    def error(self, format, *args):
        self._log(Gst.error, format, *args)

    def check_clip_values(self, clip, start, in_point, duration):
        for elem in [clip] + clip.get_children(False):
            self.check_element_values(elem, start, in_point, duration)

    def check_element_values(self, element, start, in_point, duration):
        self.assertEqual(element.props.start, start, element)
        self.assertEqual(element.props.in_point, in_point, element)
        self.assertEqual(element.props.duration, duration, element)

    def assert_effects(self, clip, *effects):
        # Make sure there are no other effects.
        self.assertEqual(set(clip.get_top_effects()), set(effects))

        # Make sure their order is correct.
        indexes = [clip.get_top_effect_index(effect)
                   for effect in effects]
        self.assertEqual(indexes, list(range(len(effects))))


class GESSimpleTimelineTest(GESTest):

    def __init__(self, *args):
        self.track_types = [GES.TrackType.AUDIO, GES.TrackType.VIDEO]
        super(GESSimpleTimelineTest, self).__init__(*args)

    def timeline_as_str(self):
        res = "====== %s =======\n" % self.timeline
        for layer in self.timeline.get_layers():
            res += "Layer %04d: " % layer.get_priority()
            for clip in layer.get_clips():
                res += "{ %s }" % clip
            res += '\n------------------------\n'

        for group in self.timeline.get_groups():
            res += "GROUP %s :" % group
            for clip in group.get_children(False):
                res += " { %s }" % clip.props.name
            res += '\n'
        res += "================================\n"
        return res

    def print_timeline(self):
        print(self.timeline_as_str())

    def setUp(self):
        self.timeline = GES.Timeline.new()
        for track_type in self.track_types:
            self.assertIn(
                track_type, [GES.TrackType.AUDIO, GES.TrackType.VIDEO])
            if track_type == GES.TrackType.AUDIO:
                self.assertTrue(self.timeline.add_track(GES.AudioTrack.new()))
            else:
                self.assertTrue(self.timeline.add_track(GES.VideoTrack.new()))

        self.assertEqual(len(self.timeline.get_tracks()),
                         len(self.track_types))
        self.layer = self.timeline.append_layer()

    def add_clip(self, start, in_point, duration, asset_type=GES.TestClip):
        clip = GES.Asset.request(asset_type, None).extract()
        clip.props.start = start
        clip.props.in_point = in_point
        clip.props.duration = duration
        self.assertTrue(self.layer.add_clip(clip))

        return clip

    def append_clip(self, layer=0, asset_type=GES.TestClip):
        while len(self.timeline.get_layers()) < layer + 1:
            self.timeline.append_layer()
        layer = self.timeline.get_layers()[layer]
        clip = GES.Asset.request(asset_type, None).extract()
        clip.props.start = layer.get_duration()
        clip.props.duration = 10
        self.assertTrue(layer.add_clip(clip))

        return clip

    def assertElementAreEqual(self, ref, element):
        self.assertTrue(isinstance(element, type(ref)), "%s and %s do not have the same type!" % (ref, element))

        props = [p for p in ref.list_properties() if p.name not in ['name']
            and not GObject.type_is_a(p.value_type, GObject.Object)]
        for p in props:
            pname = p.name
            v0 = GObject.Value()
            v0.init(p.value_type)
            v0.set_value(ref.get_property(pname))

            v1 = GObject.Value()
            v1.init(p.value_type)
            v1.set_value(element.get_property(pname))

            self.assertTrue(Gst.value_compare(v0, v1) == Gst.VALUE_EQUAL,
                "%s are not equal: %s != %s" % (pname, v0, v1))

        if isinstance(ref, GES.TrackElement):
            self.assertElementAreEqual(ref.get_nleobject(), element.get_nleobject())
            return

        if not isinstance(ref, GES.Clip):
            return

        ttypes = [track.type for track in self.timeline.get_tracks()]
        for ttype in ttypes:
            if ttypes.count(ttype) > 1:
                self.warning("Can't deeply check %s and %s "
                    "(only one track per type supported %s %s found)" % (ref,
                    element, ttypes.count(ttype), ttype))
                return

        children = element.get_children(False)
        for ref_child in ref.get_children(False):
            ref_track = ref_child.get_track()
            if not ref_track:
                self.warning("Can't check %s as not in a track" % (ref_child))
                continue

            child = None
            for tmpchild in children:
                if not isinstance(tmpchild, type(ref_child)):
                    continue

                if ref_track.type != tmpchild.get_track().type:
                    continue

                if not isinstance(ref_child, GES.Effect):
                    child = tmpchild
                    break
                elif ref_child.props.bin_description == child.props.bin_description:
                    child = tmpchild
                    break

            self.assertIsNotNone(child, "Could not find equivalent child %s in %s(%s)" % (ref_child,
                element, children))

            self.assertElementAreEqual(ref_child, child)

    def check_reload_timeline(self):
        tmpf = tempfile.NamedTemporaryFile(suffix='.xges')
        uri = Gst.filename_to_uri(tmpf.name)
        self.assertTrue(self.timeline.save_to_uri(uri, None, True))
        project = GES.Project.new(uri)
        mainloop = create_main_loop()
        def loaded_cb(unused_project, unused_timeline):
            mainloop.quit()

        project.connect("loaded", loaded_cb)
        reloaded_timeline = project.extract()

        mainloop.run()
        self.assertIsNotNone(reloaded_timeline)

        layers = self.timeline.get_layers()
        reloaded_layers = reloaded_timeline.get_layers()
        self.assertEqual(len(layers), len(reloaded_layers))
        for layer, reloaded_layer in zip(layers, reloaded_layers):
            clips = layer.get_clips()
            reloaded_clips = reloaded_layer.get_clips()
            self.assertEqual(len(clips), len(reloaded_clips))
            for clip, reloaded_clip in zip(clips, reloaded_clips):
                self.assertElementAreEqual(clip, reloaded_clip)

        return reloaded_timeline

    def assertTimelineTopology(self, topology, groups=[]):
        res = []
        for layer in self.timeline.get_layers():
            layer_timings = []
            for clip in layer.get_clips():
                layer_timings.append(
                    (type(clip), clip.props.start, clip.props.duration))
                for child in clip.get_children(True):
                    self.assertEqual(child.props.start, clip.props.start)
                    self.assertEqual(child.props.duration, clip.props.duration)

            res.append(layer_timings)
        if topology != res:
            Gst.error(self.timeline_as_str())
            self.assertEqual(topology, res)

        timeline_groups = self.timeline.get_groups()
        if groups and timeline_groups:
            for i, group in enumerate(groups):
                self.assertEqual(set(group), set(timeline_groups[i].get_children(False)))
            self.assertEqual(len(timeline_groups), i + 1)

        return res
