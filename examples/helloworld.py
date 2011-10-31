#!/usr/bin/env python

import sys

import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst

def bus_call(bus, message, loop):
    t = message.type
    if t == gst.MESSAGE_EOS:
        sys.stout.write("End-of-stream\n")
        loop.quit()
    elif t == gst.MESSAGE_ERROR:
        err, debug = message.parse_error()
        sys.stderr.write("Error: %s: %s\n" % err, debug)
        loop.quit()
    return True

def main(args):
    if len(args) != 2:
        sys.stderr.write("usage: %s <media file or uri>\n" % args[0])
        sys.exit(1)
        
    playbin = gst.element_factory_make("playbin2", None)
    if not playbin:
        sys.stderr.write("'playbin2' gstreamer plugin missing\n")
        sys.exit(1)

    # take the commandline argument and ensure that it is a uri
    if gst.uri_is_valid(args[1]):
      uri = args[1]
    else:
      uri = gst.filename_to_uri(args[1])
    playbin.set_property('uri', uri)

    # create and event loop and feed gstreamer bus mesages to it
    loop = gobject.MainLoop()

    bus = playbin.get_bus()
    bus.add_watch(bus_call, loop)
    
    # start play back and listed to events
    playbin.set_state(gst.STATE_PLAYING)
    loop.run()
    
    # cleanup
    playbin.set_state(gst.STATE_NULL)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
