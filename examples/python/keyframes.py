#!/usr/bin/env python3
#
# GStreamer
#
# Copyright (C) 2019 Thibault Saunier <tsaunier@igalia.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
# Boston, MA 02110-1335, USA.

import gi
import sys

gi.require_version('Gst', '1.0')
gi.require_version('GES', '1.0')
gi.require_version('GstController', '1.0')

from gi.repository import Gst, GES, GLib, GstController # noqa

Gst.init(None)
GES.init()


def play_timeline(timeline):
    pipeline = GES.Pipeline()
    pipeline.set_timeline(timeline)
    bus = pipeline.get_bus()
    bus.add_signal_watch()
    loop = GLib.MainLoop()
    bus.connect("message", bus_message_cb, loop, pipeline)
    pipeline.set_state(Gst.State.PLAYING)

    loop.run()

def bus_message_cb(unused_bus, message, loop, pipeline):
    if message.type == Gst.MessageType.EOS:
        print("eos")
        pipeline.set_state(Gst.State.NULL)
        loop.quit()
    elif message.type == Gst.MessageType.ERROR:
        error = message.parse_error()
        pipeline.set_state(Gst.State.NULL)
        print("error %s" % error[1])
        loop.quit()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("You must specify a file path")
        exit(-1)

    timeline = GES.Timeline.new_audio_video()

    layer = timeline.append_layer()
    clip = GES.UriClip.new(Gst.filename_to_uri(sys.argv[1]))

    # Adding clip to the layer so the TrackElements are created
    layer.add_clip(clip)

    # Create an InterpolationControlSource and make sure it interpolates linearly
    control_source = GstController.InterpolationControlSource.new()
    control_source.props.mode = GstController.InterpolationMode.LINEAR

    # Set the keyframes
    control_source.set(0, 0.0)  # Fully transparent at 0 second
    control_source.set(Gst.SECOND, 1.0)  # Fully opaque at 1 second

    # Get the video source
    video_source = clip.find_track_element(None, GES.VideoSource)
    assert(video_source)

    # And set the control source on the "alpha" property of the video source
    # Using a "direct" binding but "direct-absolute" would work the exact
    # same way as the alpha property range is [0.0 - 1.0] anyway.
    video_source.set_control_source(control_source, "alpha", "direct")

    play_timeline(timeline)