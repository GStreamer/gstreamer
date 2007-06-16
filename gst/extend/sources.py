#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2005 Edward Hervey <edward at fluendo dot com>

# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import os
import sys

import gobject
import pygst
pygst.require('0.10')
import gst

from pygobject import gsignal

EOS = 'EOS'
ERROR = 'ERROR'
WRONG_TYPE = 'WRONG_TYPE'
UNKNOWN_TYPE = 'UNKNOWN_TYPE'

class AudioSource(gst.Bin):
    """A bin for audio sources with proper audio converters"""

    gsignal('done', str)
    gsignal('prerolled')

    def __init__(self, filename, caps="audio/x-raw-int,channels=2,rate=44100"):
        # with pygtk 2.4 this call is needed for the gsignal to work
        gst.Bin.__init__(self)

        self.filename = filename
        self.outcaps = caps

        self.filesrc = gst.element_factory_make("filesrc")
        self.filesrc.set_property("location", self.filename)
        self.dbin = gst.element_factory_make("decodebin")
        self.audioconvert = gst.element_factory_make("audioconvert")
        self.audioresample = gst.element_factory_make("audioresample")
        self.volume = gst.element_factory_make("volume")
        
        self.add(self.filesrc, self.dbin,
                 self.audioconvert, self.audioresample, self.volume)
        self.filesrc.link(self.dbin)
        self.audioconvert.link(self.audioresample)
        self.audioresample.link(self.volume, caps)
        
        self.dbin.connect("new-decoded-pad", self._new_decoded_pad_cb)
        self.dbin.connect("unknown-type", self._unknown_type_cb)

        self._srcpad = None

    def __repr__(self):
        return "<AudioSource for %s>" % self.filename
        
    def set_volume(self, volume):
        gst.debug("setting volume to %f" % volume)
        self.volume.set_property("volume", volume)

    def _new_decoded_pad_cb(self, dbin, pad, is_last):
        gst.debug("new decoded pad: pad %r [%s]" % (pad, pad.get_caps().to_string()))
        if not "audio" in pad.get_caps().to_string() or self._srcpad:
            return

        gst.debug("linking pad %r to audioconvert" % pad)
        pad.link(self.audioconvert.get_pad("sink"))

        self._srcpad = gst.GhostPad("src", self.volume.get_pad("src"))
        self._srcpad.set_active(True)
        self.add_pad(self._srcpad)

    def _unknown_type_cb(self, pad, caps):
        self.emit('done', UNKNOWN_TYPE)

    def stop(self):
        self.set_state(gst.STATE_NULL)

    def clean(self):
        self.stop()
        self.remove(self.filesrc)
        self.remove(self.dbin)
        self.remove(self.audioconvert)
        self.remove(self.audioresample)
        self.remove(self.volume)
        self.filesrc = None
        self.dbin = None
        self.audioconvert = None
        self.volume = None
        
gobject.type_register(AudioSource)

       
# run us to test
if __name__ == "__main__":
    main = gobject.MainLoop()

    def _done_cb(source, reason):
        print "Done"
        sys.stdout.flush()
        if reason != EOS:
            print "Some error happened: %s" % reason
        main.quit()

    def _error_cb(source, element, gerror, message):
        print "Error: %s" % gerror
        main.quit()
        
    try:
        source = AudioSource(sys.argv[1])
    except IndexError:
        sys.stderr.write("Please give a filename to play\n")
        sys.exit(1)

    pipeline = gst.Pipeline("playing")
    # connecting on the source also catches eos emit when
    # no audio pad
    source.connect('done', _done_cb)
    pipeline.connect('error', _error_cb)

    p = "osssink"
    if len(sys.argv) > 2:
        p = " ".join(sys.argv[2:])
    
    pipeline.add(source)
    sink = gst.parse_launch(p)
    pipeline.add(sink)
    source.link(sink)

    # we schedule this as a timeout so that we are definately in the main
    # loop when it goes to PLAYING, and errors make main.quit() work correctly
    def _start(pipeline):
        print "setting pipeline to PLAYING"
        pipeline.set_state(gst.STATE_PLAYING)
        print "set pipeline to PLAYING"

    gobject.timeout_add(0, _start, pipeline)
    gobject.idle_add(pipeline.iterate)

    print "Going into main loop"
    main.run()
    print "Left main loop"

    pipeline.set_state(gst.STATE_NULL)
    pipeline.remove(source)
    pipeline.remove(sink)
    utils.gc_collect('cleaned out pipeline')
    source.clean()
    utils.gc_collect('cleaned up source')
    source = None
    utils.gc_collect('set source to None')
