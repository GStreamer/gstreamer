#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# audio concat tool
# takes in one or more audio files and creates one audio file of the combination

# Uses the gnonlin elements (http://gnonlin.sf.net/)

import os
import sys
import gobject
import gst

from gstfile import Discoverer, time_to_string

class AudioSource(gst.Bin):
    """A bin for audio sources with proper audio converters"""

    def __init__(self, filename, caps):
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

    def _new_decoded_pad_cb(self, dbin, pad, is_last):
        if not "audio" in pad.get_caps().to_string():
            return
        pad.link(self.audioconvert.get_pad("sink"))

gobject.type_register(AudioSource)

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

        ## identity perfect stream check !
        identity = gst.element_factory_make("identity")
        identity.set_property("check-perfect", True)
        self.add(identity)
        
        #self.audioconvert.link(self.audioenc)
        if not self.audioconvert.link(identity):
            print "couldn't link audioconv -> ident"
        if not identity.link(self.audioenc):
            print "couldn't link ident -> audioenc"
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
            asource = AudioSource(infile, caps)
            gnlsource = gst.element_factory_make("gnlsource")
            gnlsource.set_property("element", asource)
            gnlsource.set_property("media-start", 0L)
            gnlsource.set_property("media-stop", d.audiolength)
            gnlsource.set_property("start", pos)
            gnlsource.set_property("stop", pos + d.audiolength)
            self.audiocomp.add(gnlsource)
            pos += d.audiolength

        self.timeline.get_pad("src_audiocomp").link(self.audioconvert.get_pad("sink"))
        timelineprobe = gst.Probe(False, self.timelineprobe)
        self.timeline.get_pad("src_audiocomp").add_probe(timelineprobe)

    def timelineprobe(self, probe, data):
        if isinstance(data, gst.Buffer):
            print "timeline outputs buffer", data.timestamp, data.duration
        else:
            print "timeline ouputs event", data.type
        return True

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
