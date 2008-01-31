#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# videomixer-controller.py
# (c) 2008 Stefan Kost <ensonic@users.sf.net>
# Test case for the GstController using videomixer and videotestsrc

import pygst
pygst.require('0.10')
import gst
import time

def main():
    pipeline = gst.Pipeline("videocontroller")
    src = gst.element_factory_make("videotestsrc", "src")
    mix = gst.element_factory_make("videomixer", "mix")
    conv = gst.element_factory_make("ffmpegcolorspace", "conv")
    sink = gst.element_factory_make("autovideosink", "sink")
    pipeline.add(src, mix, conv, sink)

    spad = src.get_static_pad('src')
    dpad = mix.get_request_pad('sink_%d')

    spad.link(dpad)
    mix.link(conv)
    conv.link(sink)

    control = gst.Controller(dpad, "xpos", "ypos")
    control.set_interpolation_mode("xpos", gst.INTERPOLATE_LINEAR)
    control.set_interpolation_mode("ypos", gst.INTERPOLATE_LINEAR)

    control.set("xpos", 0, 0)
    control.set("xpos", 5 * gst.SECOND, 200)

    control.set("ypos", 0, 0)
    control.set("ypos", 5 * gst.SECOND, 200)

    pipeline.set_state(gst.STATE_PLAYING)

    time.sleep(7)

if __name__ == "__main__":
    main()
