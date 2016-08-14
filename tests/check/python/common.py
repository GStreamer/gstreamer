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
import unittest  # noqa
import tempfile  # noqa

Gst.init(None)
GES.init()


def create_main_loop():
    """Creates a MainLoop with a timeout."""
    mainloop = GLib.MainLoop()
    timed_out = False

    def quit_cb(unused):
        nonlocal timed_out
        timed_out = True
        mainloop.quit()

    def run(timeout_seconds=5):
        source = GLib.timeout_source_new_seconds(timeout_seconds)
        source.set_callback(quit_cb)
        source.attach()
        GLib.MainLoop.run(mainloop)
        source.destroy()
        if timed_out:
            raise Exception("Timed out after %s seconds" % timeout_seconds)

    mainloop.run = run
    return mainloop

def create_project(with_group=False, saved=False):
    """Creates a project with two clips in a group."""
    project = GES.Project()
    timeline = project.extract()
    layer = timeline.append_layer()

    if with_group:
        clip1 = GES.TitleClip()
        clip1.set_start(0)
        clip1.set_duration(10)
        layer.add_clip(clip1)
        clip2 = GES.TitleClip()
        clip2.set_start(100)
        clip2.set_duration(10)
        layer.add_clip(clip2)
        group = GES.Container.group([clip1, clip2])

    if saved:
        uri = "file://%s" % tempfile.NamedTemporaryFile(suffix=".xges").name
        project.save(timeline, uri, None, overwrite=True)

    return timeline

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


class GESSimpleTimelineTest(GESTest):
    def setUp(self):
        self.timeline = GES.Timeline.new_audio_video()
        self.assertEqual(len(self.timeline.get_tracks()), 2)
        self.layer = self.timeline.append_layer()

    def add_clip(self, start, in_point, duration):
        clip = GES.TestClip()
        clip.props.start = start
        clip.props.in_point = in_point
        clip.props.duration = duration
        self.layer.add_clip(clip)

        return clip

    def check_clip_values(self, clip, start, in_point, duration):
        for elem in [clip] + clip.get_children(False):
            self.check_element_values(elem, start, in_point, duration)

    def check_element_values(self, element, start, in_point, duration):
        self.assertEqual(element.props.start, start, element)
        self.assertEqual(element.props.in_point, in_point, element)
        self.assertEqual(element.props.duration, duration, element)
