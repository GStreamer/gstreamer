#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2005 Edward Hervey <edward@fluendo.com>

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

# audio concat tool
# takes in one or more audio files and creates one audio file of the combination

# Uses the gnonlin elements (http://gnonlin.sf.net/)

import os
import sys
import gobject
import gst
from gst.extend import sources

from gstfile import Discoverer, time_to_string

class AudioConcat(gst.Thread):
    """A Gstreamer thread that concatenates a series of audio files to another audio file"""

    def __init__(self, infiles, outfile, audioenc="rawvorbisenc", muxer="oggmux"):
        gst.Thread.__init__(self)
        self.infiles = infiles
        self.outfile = outfile
        self.audioenc = gst.element_factory_make(audioenc)
        if not self.audioenc:
            raise NameError, str(audioenc + " audio encoder is not available")
        self.muxer = gst.element_factory_make(muxer)
        if not self.muxer:
            raise NameError, str(muxer + " muxer is not available")
        self.filesink = gst.element_factory_make("filesink")
        self.filesink.set_property("location", self.outfile)

        self.timeline = gst.element_factory_make("gnltimeline")
        self.audiocomp = gst.element_factory_make("gnlcomposition", "audiocomp")

        self.audioconvert = gst.element_factory_make("audioconvert")
        self.add_many(self.timeline, self.audioconvert,
                      self.audioenc, self.muxer, self.filesink)
        self.audioconvert.link(self.audioenc)
        self.audioenc.link(self.muxer)
        self.muxer.link(self.filesink)

        self.timeline.add(self.audiocomp)

        caps = gst.caps_from_string("audio/x-raw-int,channels=2,rate=44100,depth=16")
        pos = 0L
        for infile in self.infiles:
            d = Discoverer(infile)
            if not d.audiolength:
                continue
            print "file", infile, "has length", time_to_string(d.audiolength)
            asource = sources.AudioSource(infile, caps)
            gnlsource = gst.element_factory_make("gnlsource")
            gnlsource.set_property("element", asource)
            gnlsource.set_property("media-start", 0L)
            gnlsource.set_property("media-stop", d.audiolength)
            gnlsource.set_property("start", pos)
            gnlsource.set_property("stop", pos + d.audiolength)
            self.audiocomp.add(gnlsource)
            pos += d.audiolength

        self.timeline.get_pad("src_audiocomp").link(self.audioconvert.get_pad("sink"))

gobject.type_register(AudioConcat)

def eos_cb(pipeline):
    sys.exit()

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print "Usage : %s <input file(s)> <output file>" % sys.argv[0]
        print "\tCreates an ogg file from all the audio input files"
        sys.exit()
    if not gst.element_factory_make("gnltimeline"):
        print "You need the gnonlin elements installed (http://gnonlin.sf.net/)"
        sys.exit()
    concat = AudioConcat(sys.argv[1:-1], sys.argv[-1])
    concat.connect("eos", eos_cb)
    concat.set_state(gst.STATE_PLAYING)
    gst.main()
