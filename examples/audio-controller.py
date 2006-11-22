#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# audio-controller.py
# (c) 2005 Edward Hervey <edward at fluendo dot com>
# Test case for the GstController on sinesrc -> alsasink
# Inspired from ensonic's examples/controller/audio-controller.c

import pygst
pygst.require('0.10')
import gst
import time

def main():
    pipeline = gst.Pipeline("audiocontroller")
    src = gst.element_factory_make("audiotestsrc", "src")
    sink = gst.element_factory_make("alsasink", "sink")
    pipeline.add(src, sink)
    src.link(sink)

    control = gst.Controller(src, "freq", "volume")
    control.set_interpolation_mode("volume", gst.INTERPOLATE_LINEAR)
    control.set_interpolation_mode("freq", gst.INTERPOLATE_LINEAR)

    control.set("volume", 0, 0.0)
    control.set("volume", 2 * gst.SECOND, 1.0)
    control.set("volume", 4 * gst.SECOND, 0.0)
    control.set("volume", 6 * gst.SECOND, 1.0)

    control.set("freq", 0, 440.0)
    control.set("freq", 3 * gst.SECOND, 3000.0)
    control.set("freq", 6 * gst.SECOND, 880.0)

    pipeline.set_state(gst.STATE_PLAYING)

    time.sleep(7)

if __name__ == "__main__":
    main()
