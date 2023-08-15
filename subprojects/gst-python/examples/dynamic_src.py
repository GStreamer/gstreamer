#!/usr/bin/env python3

'''
Simple example to demonstrate dynamically adding and removing source elements
to a playing pipeline.
'''

import sys
import random

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GLib', '2.0')
from gi.repository import GLib, Gst


class ProbeData:
    def __init__(self, pipe, src):
        self.pipe = pipe
        self.src = src


def bus_call(bus, message, loop):
    t = message.type
    if t == Gst.MessageType.EOS:
        sys.stdout.write("End-of-stream\n")
        loop.quit()
    elif t == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        sys.stderr.write("Error: %s: %s\n" % (err, debug))
        loop.quit()
    return True


def dispose_src_cb(src):
    src.set_state(Gst.State.NULL)


def probe_cb(pad, info, pdata):
    peer = pad.get_peer()
    pad.unlink(peer)
    pdata.pipe.remove(pdata.src)
    # Can't set the state of the src to NULL from its streaming thread
    GLib.idle_add(dispose_src_cb, pdata.src)

    pdata.src = Gst.ElementFactory.make('videotestsrc')
    pdata.src.props.pattern = random.randint(0, 24)
    pdata.src.props.is_live = True
    pdata.pipe.add(pdata.src)
    srcpad = pdata.src.get_static_pad("src")
    srcpad.link(peer)
    pdata.src.sync_state_with_parent()

    GLib.timeout_add_seconds(1, timeout_cb, pdata)

    return Gst.PadProbeReturn.REMOVE


def timeout_cb(pdata):
    srcpad = pdata.src.get_static_pad('src')
    srcpad.add_probe(Gst.PadProbeType.IDLE, probe_cb, pdata)
    return GLib.SOURCE_REMOVE


def main(args):
    Gst.init(None)

    pipe = Gst.Pipeline.new('dynamic')
    src = Gst.ElementFactory.make('videotestsrc')
    sink = Gst.ElementFactory.make('autovideosink')
    pipe.add(src, sink)
    src.link(sink)

    pdata = ProbeData(pipe, src)

    loop = GLib.MainLoop()

    GLib.timeout_add_seconds(1, timeout_cb, pdata)

    bus = pipe.get_bus()
    bus.add_signal_watch()
    bus.connect("message", bus_call, loop)

    # start play back and listen to events
    pipe.set_state(Gst.State.PLAYING)
    try:
        loop.run()
    except e:
        pass

    # cleanup
    pipe.set_state(Gst.State.NULL)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
