#!/usr/bin/env python

import sys

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GLib', '2.0')
from gi.repository import GLib, Gst


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


def main(args):
    if len(args) != 2:
        sys.stderr.write("usage: %s <media file or uri>\n" % args[0])
        sys.exit(1)

    Gst.init(None)

    playbin = Gst.ElementFactory.make("playbin", None)
    if not playbin:
        sys.stderr.write("'playbin' gstreamer plugin missing\n")
        sys.exit(1)

    # take the commandline argument and ensure that it is a uri
    if Gst.uri_is_valid(args[1]):
        uri = args[1]
    else:
        uri = Gst.filename_to_uri(args[1])
    playbin.set_property('uri', uri)

    # create and event loop and feed gstreamer bus mesages to it
    loop = GLib.MainLoop()

    bus = playbin.get_bus()
    bus.add_signal_watch()
    bus.connect("message", bus_call, loop)

    # start play back and listed to events
    playbin.set_state(Gst.State.PLAYING)
    try:
        loop.run()
    except e:
        pass

    # cleanup
    playbin.set_state(Gst.State.NULL)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
