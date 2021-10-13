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

from urllib.parse import urlparse
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

try:
    gi.require_version("GstTranscoder", "1.0")
    from gi.repository import GstTranscoder
except ValueError:
    GstTranscoder = None

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


def can_generate_assets():
    if GstTranscoder is None:
        return False, "GstTranscoder is not available"

    if not Gst.ElementFactory.make("testsrcbin"):
        return False, "testbinsrc is not available"

    return True, None


@contextlib.contextmanager
def created_video_asset(uri=None, num_bufs=30, framerate="30/1"):
    with tempfile.NamedTemporaryFile(suffix=".ogg") as f:
        if not uri:
            uri = Gst.filename_to_uri(f.name)
            name = f.name
        else:
            name = urlparse(uri).path
        pipe = Gst.parse_launch(f"videotestsrc num-buffers={num_bufs} ! video/x-raw,framerate={framerate} ! theoraenc ! oggmux ! filesink location={name}")
        pipe.set_state(Gst.State.PLAYING)
        assert pipe.get_bus().timed_pop_filtered(Gst.CLOCK_TIME_NONE, Gst.MessageType.EOS)
        pipe.set_state(Gst.State.NULL)

        yield uri


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

    def assertGESError(self, error, code, message=""):
        if error is None:
            raise AssertionError(
                "{}{}Received no error".format(message, message and ": "))
        if error.domain != "GES_ERROR":
            raise AssertionError(
                "{}{}Received error ({}) in domain {} rather than "
                "GES_ERROR".format(
                    message, message and ": ", error.message, error.domain))
        err_code = GES.Error(error.code)
        if err_code != code:
            raise AssertionError(
                "{}{}Received {} error ({}) rather than {}".format(
                    message, message and ": ", err_code.value_name,
                    error.message, code.value_name))

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

    def append_clip(self, layer=0, asset_type=GES.TestClip, asset_id=None):
        while len(self.timeline.get_layers()) < layer + 1:
            self.timeline.append_layer()
        layer = self.timeline.get_layers()[layer]
        if asset_type == GES.UriClip:
            asset = GES.UriClipAsset.request_sync(asset_id)
        else:
            asset = GES.Asset.request(asset_type, asset_id)
        clip = asset.extract()
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
            refval = GObject.Value()
            refval.init(p.value_type)
            refval.set_value(ref.get_property(pname))

            value = GObject.Value()
            value.init(p.value_type)
            value.set_value(element.get_property(pname))

            self.assertTrue(Gst.value_compare(refval, value) == Gst.VALUE_EQUAL,
                "%s are not equal: %s != %s\n    %s != %s" % (pname, value, refval, element, ref))

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
                elif ref_child.props.bin_description == tmpchild.props.bin_description:
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


class GESTimelineConfigTest(GESTest):
    """
    Tests where all the configuration changes, snapping positions and
    auto-transitions are accounted for.
    """

    def setUp(self):
        timeline = GES.Timeline.new()
        self.timeline = timeline
        timeline.set_auto_transition(True)

        self.snap_occured = False
        self.snap = None

        def snap_started(tl, el1, el2, pos):
            if self.snap_occured:
                raise AssertionError(
                    "Previous snap {} not accounted for".format(self.snap))
            self.snap_occured = True
            if self.snap is not None:
                raise AssertionError(
                    "Previous snap {} not ended".format(self.snap))
            self.snap = (el1.get_parent(), el2.get_parent(), pos)

        def snap_ended(tl, el1, el2, pos):
            self.assertEqual(
                self.snap, (el1.get_parent(), el2.get_parent(), pos))
            self.snap = None

        timeline.connect("snapping-started", snap_started)
        timeline.connect("snapping-ended", snap_ended)

        self.lost_clips = []

        def unrecord_lost_clip(layer, clip):
            if clip in self.lost_clips:
                self.lost_clips.remove(clip)

        def record_lost_clip(layer, clip):
            self.lost_clips.append(clip)

        def layer_added(tl, layer):
            layer.connect("clip-added", unrecord_lost_clip)
            layer.connect("clip-removed", record_lost_clip)

        timeline.connect("layer-added", layer_added)

        self.clips = []
        self.auto_transitions = {}
        self.config = {}

    @staticmethod
    def new_config(start, duration, inpoint, maxduration, layer):
        return {"start": start, "duration": duration, "in-point": inpoint,
                "max-duration": maxduration, "layer": layer}

    def add_clip(self, name, layer, tracks, start, duration, inpoint=0,
                 maxduration=Gst.CLOCK_TIME_NONE, clip_type=GES.TestClip,
                 asset_id=None, effects=None):
        """
        Create a clip with the given @name and properties and add it to the
        layer of priority @layer to the tracks in @tracks. Also registers
        its expected configuration.
        """
        if effects is None:
            effects = []

        lay = self.timeline.get_layer(layer)
        while lay is None:
            self.timeline.append_layer()
            lay = self.timeline.get_layer(layer)

        asset = GES.Asset.request(clip_type, asset_id)
        clip = asset.extract()
        self.assertTrue(clip.set_name(name))
        # FIXME: would be better to use select-tracks-for-object
        # hack around the fact that we cannot use select-tracks-for-object
        # in python by setting start to large number to ensure no conflict
        # when adding a clip
        self.assertTrue(clip.set_start(10000))
        self.assertTrue(clip.set_duration(duration))
        self.assertTrue(clip.set_inpoint(inpoint))

        for effect in effects:
            self.assertTrue(clip.add(effect))

        if lay.add_clip(clip) != True:
            raise AssertionError(
                "Failed to add clip {} to layer {}".format(name, layer))

        # then remove the children not in the selected tracks, which may
        # now allow some clips to fully/triple overlap because they do
        # not share a track
        for child in clip.get_children(False):
            if child.get_track() not in tracks:
                clip.remove(child)

        # then move to the desired start
        prev_snap = self.timeline.get_snapping_distance()
        self.timeline.set_snapping_distance(0)
        self.assertTrue(clip.set_start(start))
        self.timeline.set_snapping_distance(prev_snap)

        self.assertTrue(clip.set_max_duration(maxduration))

        self.config[clip] = self.new_config(
            start, duration, inpoint, maxduration, layer)
        self.clips.append(clip)

        return clip

    def add_group(self, name, to_group):
        """
        Create a group with the given @name and the elements in @to_group.
        Also registers its expected configuration.
        """
        group = GES.Group.new()
        self.assertTrue(group.set_name(name))
        start = None
        end = None
        layer = None
        for element in to_group:
            if start is None:
                start = element.start
                end = element.start + element.duration
                layer = element.get_layer_priority()
            else:
                start = min(start, element.start)
                end = max(end, element.start + element.duration)
                layer = min(layer, element.get_layer_priority())
            self.assertTrue(group.add(element))

        self.config[group] = self.new_config(
            start, end - start, 0, Gst.CLOCK_TIME_NONE, layer)
        return group

    def register_auto_transition(self, clip1, clip2, track):
        """
        Register that we expect an auto-transition to exist between
        @clip1 and @clip2 in @track.
        """
        transition = self._find_transition(clip1, clip2, track)
        if transition is None:
            raise AssertionError(
                "{} and {} have no auto-transition in track {}".format(
                    clip1, clip2, track))
        if transition in self.auto_transitions.values():
            raise AssertionError(
                "Auto-transition between {} and {} in track {} already "
                "registered".format(clip1, clip2, track))
        key = (clip1, clip2, track)
        if key in self.auto_transitions:
            raise AssertionError(
                "Auto-transition already registered for {}".format(key))

        self.auto_transitions[key] = transition

    def add_video_track(self):
        track = GES.VideoTrack.new()
        self.assertTrue(self.timeline.add_track(track))
        return track

    def add_audio_track(self):
        track = GES.AudioTrack.new()
        self.assertTrue(self.timeline.add_track(track))
        return track

    def assertElementConfig(self, element, config):
        for prop in config:
            if prop == "layer":
                val = element.get_layer_priority()
            else:
                val = element.get_property(prop)

            if val != config[prop]:
                raise AssertionError("{} property {}: {} != {}".format(
                    element, prop, val, config[prop]))

    @staticmethod
    def _source_in_track(clip, track):
        if clip.find_track_element(track, GES.Source):
            return True
        return False

    def _find_transition(self, clip1, clip2, track):
        """find transition from earlier clip1 to later clip2"""
        if not self._source_in_track(clip1, track) or \
                not self._source_in_track(clip2, track):
            return None

        layer_prio = clip1.get_layer_priority()
        if layer_prio != clip2.get_layer_priority():
            return None

        if clip1.start >= clip2.start:
            return None

        start = clip2.start
        end = clip1.start + clip1.duration
        if start >= end:
            return None
        duration = end - start

        layer = self.timeline.get_layer(layer_prio)
        self.assertIsNotNone(layer)

        for clip in layer.get_clips():
            children = clip.get_children(False)
            if len(children) == 1:
                child = children[0]
            else:
                continue
            if isinstance(clip, GES.TransitionClip) and clip.start == start \
                    and clip.duration == duration and child.get_track() == track:
                return clip

        raise AssertionError(
            "No auto-transition between {} and {} in track {}".format(
                clip1, clip2, track))

    def _transition_between(self, new, existing, clip1, clip2, track):
        if clip1.start < clip2.start:
            entry = (clip1, clip2, track)
        else:
            entry = (clip2, clip1, track)
        trans = self._find_transition(*entry)

        if trans is None:
            return

        if entry in new:
            new.remove(entry)
            self.auto_transitions[entry] = trans
        elif entry in existing:
            existing.remove(entry)
            expect = self.auto_transitions[entry]
            if trans != expect:
                raise AssertionError(
                    "Auto-transition between {} and {} in track {} changed "
                    "from {} to {}".format(
                        clip1, clip2, track, expect, trans))
        else:
            raise AssertionError(
                "Unexpected transition found between {} and {} in track {}"
                "".format(clip1, clip2, track))

    def assertTimelineConfig(
            self, new_props=None, snap_position=None, snap_froms=None,
            snap_tos=None, new_transitions=None, lost_transitions=None):
        """
        Check that the timeline configuration has only changed by the
        differences present in @new_props.
        Check that a snap occurred at @snap_position between one of the
        clips in @snap_froms and one of the clips in @snap_tos.
        Check that all new transitions in the timeline are present in
        @new_transitions.
        Checl that all the transitions that were lost are in
        @lost_transitions.
        """
        if new_props is None:
            new_props = {}
        if snap_froms is None:
            snap_froms = []
        if snap_tos is None:
            snap_tos = []
        if new_transitions is None:
            new_transitions = []
        if lost_transitions is None:
            lost_transitions = []

        for element, config in new_props.items():
            if element not in self.config:
                self.config[element] = {}

            for prop in config:
                self.config[element][prop] = new_props[element][prop]

        for element, config in self.config.items():
            self.assertElementConfig(element, config)

        # check that snapping occurred
        snaps = []
        for snap_from in snap_froms:
            for snap_to in snap_tos:
                snaps.append((snap_from, snap_to, snap_position))

        if self.snap is None:
            if snaps:
                raise AssertionError(
                    "No snap occurred, but expected a snap in {}".format(snaps))
        elif not snaps:
            if self.snap_occured:
                raise AssertionError(
                    "Snap {} occurred, but expected no snap".format(self.snap))
        elif self.snap not in snaps:
            raise AssertionError(
                "Snap {} occurred, but expected a snap in {}".format(
                    self.snap, snaps))
        self.snap_occured = False

        # check that lost transitions are not part of the layer
        for clip1, clip2, track in lost_transitions:
            key = (clip1, clip2, track)
            if key not in self.auto_transitions:
                raise AssertionError(
                    "No such auto-transition between {} and {} in track {} "
                    "is registered".format(clip1, clip2, track))
            # make sure original transition was removed from the layer
            trans = self.auto_transitions[key]
            if trans not in self.lost_clips:
                raise AssertionError(
                    "The auto-transition {} between {} and {} track {} was "
                    "not removed from the layers, but expect it to be lost"
                    "".format(trans, clip1, clip2, track))
            self.lost_clips.remove(trans)
            # make sure a new one wasn't created
            trans = self._find_transition(clip1, clip2, track)
            if trans is not None:
                raise AssertionError(
                    "Found auto-transition between {} and {} in track {} "
                    "is present, but expected it to be lost".format(
                        clip1, clip2, track))
            # since it was lost, remove it
            del self.auto_transitions[key]

        # check that all lost clips are accounted for
        if self.lost_clips:
            raise AssertionError(
                "Clips were lost that are not accounted for: {}".format(
                    self.lost_clips))

        # check that all other transitions are either existing ones or
        # new ones
        new = set(new_transitions)
        existing = set(self.auto_transitions.keys())
        for i, clip1 in enumerate(self.clips):
            for clip2 in self.clips[i+1:]:
                for track in self.timeline.get_tracks():
                    self._transition_between(
                        new, existing, clip1, clip2, track)

        # make sure we are not missing any expected transitions
        if new:
            raise AssertionError(
                "Did not find new transitions for {}".format(new))
        if existing:
            raise AssertionError(
                "Did not find existing transitions for {}".format(existing))

        # make sure there aren't any clips we are unaware of
        transitions = self.auto_transitions.values()
        for layer in self.timeline.get_layers():
            for clip in layer.get_clips():
                if clip not in self.clips and clip not in transitions:
                    raise AssertionError("Unknown clip {}".format(clip))

    def assertEdit(self, element, layer, mode, edge, position, snap,
                   snap_froms, snap_tos, new_props, new_transitions,
                   lost_transitions):
        if not element.edit_full(layer, mode, edge, position):
            raise AssertionError(
                "Edit of {} to layer {}, mode {}, edge {}, at position {} "
                "failed when a success was expected".format(
                    element, layer, mode, edge, position))
        self.assertTimelineConfig(
            new_props=new_props, snap_position=snap, snap_froms=snap_froms,
            snap_tos=snap_tos, new_transitions=new_transitions,
            lost_transitions=lost_transitions)

    def assertFailEdit(self, element, layer, mode, edge, position, err_code):
        res = None
        error = None
        try:
            res = element.edit_full(layer, mode, edge, position)
        except GLib.Error as exception:
            error = exception

        if err_code is None:
            if res is not False:
                raise AssertionError(
                    "Edit of {} to layer {}, mode {}, edge {}, at "
                    "position {} succeeded when a failure was expected"
                    "".format(
                        element, layer, mode, edge, position))
            if error is not None:
                raise AssertionError(
                    "Edit of {} to layer {}, mode {}, edge {}, at "
                    "position {} did produced an error when none was "
                    "expected".format(
                        element, layer, mode, edge, position))
        else:
            self.assertGESError(
                error, err_code,
                "Edit of {} to layer {}, mode {}, edge {}, at "
                "position {}".format(element, layer, mode, edge, position))
        # should be no change or snapping if edit fails
        self.assertTimelineConfig()
