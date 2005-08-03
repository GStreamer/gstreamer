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
import gst

class AudioSource(gst.Bin):
    """A bin for audio sources with proper audio converters"""

    def __init__(self, filename, caps="audio/x-raw-int,channels=2,rate=44100"):
        gst.Bin.__init__(self)
        self.filename = filename
        self.outcaps = caps

        self.filesrc = gst.element_factory_make("filesrc")
        self.filesrc.set_property("location", self.filename)
        self.dbin = gst.element_factory_make("decodebin")
        self.ident = gst.element_factory_make("identity")
        self.audioconvert = gst.element_factory_make("audioconvert")
        self.audioscale = gst.element_factory_make("audioscale")
        
        self.add_many(self.filesrc, self.dbin, self.ident,
                      self.audioconvert, self.audioscale)
        self.filesrc.link(self.dbin)
        self.audioconvert.link(self.audioscale)
        self.audioscale.link(self.ident, caps)
        self.add_ghost_pad(self.ident.get_pad("src"), "src")
        
        self.dbin.connect("new-decoded-pad", self._new_decoded_pad_cb)
        self.eos = False
        self.connect("eos", self._have_eos_cb)

    def _new_decoded_pad_cb(self, dbin, pad, is_last):
        if not "audio" in pad.get_caps().to_string():
            return
        pad.link(self.audioconvert.get_pad("sink"))

    def _have_eos_cb(self, object):
        self.eos = True

# run us to test
gobject.type_register(AudioSource)

if __name__ == "__main__":
    source = AudioSource(sys.argv[1])
    thread = gst.Thread("playing")

    pipeline = "osssink"
    if len(sys.argv) > 2:
        pipeline = " ".join(sys.argv[2:])
    
    thread.add(source)
    sink = gst.parse_launch(pipeline)
    thread.add(sink)
    source.link(sink)

    thread.set_state(gst.STATE_PLAYING)

    while not source.eos:
        pass

    thread.set_state(gst.STATE_NULL)
