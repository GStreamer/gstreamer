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

# gstfile.py
# Discovers and prints out multimedia information of files

# This example shows how to use gst-python:
# _ in an object-oriented way (Discoverer class)
# _ subclassing a gst.Pipeline
# _ and overidding existing methods (do_iterate())

import os
import sys

import gobject
import gst

def time_to_string(value):
    """
    transform a value in nanoseconds into a human-readable string
    """
    ms = value / gst.MSECOND
    sec = ms / 1000
    ms = ms % 1000
    min = sec / 60
    sec = sec % 60
    return "%2dm %2ds %3d" % (min, sec, ms)


class Discoverer(gst.Pipeline):
    """
    Discovers information about files
    """
    mimetype = None

    audiocaps = {}
    videocaps = {}

    videowidth = 0
    videoheight = 0
    videorate = 0

    audiofloat = False
    audiorate = 0
    audiodepth = 0
    audiowidth = 0
    audiochannels = 0

    audiolength = 0L
    videolength = 0L

    is_video = False
    is_audio = False

    otherstreams = []

    finished = False
    tags = {}


    def __init__(self, filename):
        gobject.GObject.__init__(self)

        self.mimetype = None

        self.audiocaps = {}
        self.videocaps = {}

        self.videowidth = 0
        self.videoheight = 0
        self.videorate = 0

        self.audiofloat = False
        self.audiorate = 0
        self.audiodepth = 0
        self.audiowidth = 0
        self.audiochannels = 0

        self.audiolength = 0L
        self.videolength = 0L

        self.is_video = False
        self.is_audio = False

        self.otherstreams = []

        self.finished = False
        self.tags = {}
        
        if not os.path.isfile(filename):
            self.finished = True
            return
        
        # the initial elements of the pipeline
        self.src = gst.element_factory_make("filesrc")
        self.src.set_property("location", filename)
        self.src.set_property("blocksize", 1000000)
        self.dbin = gst.element_factory_make("decodebin")
        self.add_many(self.src, self.dbin)
        self.src.link(self.dbin)
        self.typefind = self.dbin.get_by_name("typefind")

        # callbacks
        self.typefind.connect("have-type", self._have_type_cb)
        self.dbin.connect("new-decoded-pad", self._new_decoded_pad_cb)
        self.dbin.connect("unknown-type", self._unknown_type_cb)
        self.dbin.connect("found-tag", self._found_tag_cb)

        self.discover()

    def discover(self):
        """iterate on ourself to find the information on the given file"""
        if self.finished:
            return
        self.set_state(gst.STATE_PLAYING)
        while 1:
            if not self.iterate():
                break
        self.set_state(gst.STATE_NULL)
        self.finished = True

    def print_info(self):
        """prints out the information on the given file"""
        if not self.finished:
            self.discover()
        if not self.mimetype:
            print "Unknown media type"
            return
        print "Mime Type :\t", self.mimetype
        if not self.is_video and not self.is_audio:
            return
        print "Length :\t", time_to_string(max(self.audiolength, self.videolength))
        print "\tAudio:", time_to_string(self.audiolength), "\tVideo:", time_to_string(self.videolength)
        if self.is_video:
            print "Video :"
            print "\t%d x %d @ %.2f fps" % (self.videowidth,
                                            self.videoheight,
                                            self.videorate)
            if self.tags.has_key("video-codec"):
                print "\tCodec :", self.tags.pop("video-codec")
        if self.is_audio:
            print "Audio :"
            if self.audiofloat:
                print "\t%d channels(s) : %dHz @ %dbits (float)" % (self.audiochannels,
                                                                    self.audiorate,
                                                                    self.audiowidth)
            else:
                print "\t%d channels(s) : %dHz @ %dbits (int)" % (self.audiochannels,
                                                                  self.audiorate,
                                                                  self.audiodepth)
            if self.tags.has_key("audio-codec"):
                print "\tCodec :", self.tags.pop("audio-codec")
        for stream in self.otherstreams:
            if not stream == self.mimetype:
                print "Other unsuported Multimedia stream :", stream
        if self.tags:
            print "Additional information :"
            for tag in self.tags.keys():
                print "%20s :\t" % tag, self.tags[tag]

    def _unknown_type_cb(self, dbin, pad, caps):
        self.otherstreams.append(caps.to_string())

    def _have_type_cb(self, typefind, prob, caps):
        self.mimetype = caps.to_string()

    def _notify_caps_cb(self, pad, args):
        caps = pad.get_negotiated_caps()
        if not caps:
            return
        # the caps are fixed
        # We now get the total length of that stream
        length = pad.get_peer().query(gst.QUERY_TOTAL, gst.FORMAT_TIME)
        # We store the caps and length in the proper location
        if "audio" in caps.to_string():
            self.audiocaps = caps
            self.audiolength = length
            self.audiorate = caps[0]["rate"]
            self.audiowidth = caps[0]["width"]
            self.audiochannels = caps[0]["channels"]
            if "x-raw-float" in caps.to_string():
                self.audiofloat = True
            else:
                self.audiodepth = caps[0]["depth"]
            if (not self.is_video) or self.videocaps:
                self.finished = True
        elif "video" in caps.to_string():
            self.videocaps = caps
            self.videolength = length
            self.videowidth = caps[0]["width"]
            self.videoheight = caps[0]["height"]
            self.videorate = caps[0]["framerate"]
            if (not self.is_audio) or self.audiocaps:
                self.finished = True

    def _new_decoded_pad_cb(self, dbin, pad, is_last):
        # Does the file contain got audio or video ?
        if "audio" in pad.get_caps().to_string():
            self.is_audio = True
        elif "video" in pad.get_caps().to_string():
            self.is_video = True
        if is_last and not self.is_video and not self.is_audio:
            self.finished = True
            return
        # we connect a fakesink to the new pad...
        fakesink = gst.element_factory_make("fakesink")
        self.add(fakesink)
        sinkpad = fakesink.get_pad("sink")
        # ... and connect a callback for when the caps are fixed
        sinkpad.connect("notify::caps", self._notify_caps_cb)
        pad.link(sinkpad)
        fakesink.set_state(gst.STATE_PLAYING)

    def _found_tag_cb(self, dbin, source, tags):
        self.tags.update(tags)

    def do_iterate(self):
        # this overrides the GstBin 'iterate' method
        # if we have finished discovering we stop the iteration
        if self.finished:
            return False
        # else we call the parent class method
        return gst.Pipeline.do_iterate(self)

gobject.type_register(Discoverer)

def main(args):
    if len(args) < 2:
        print 'usage: %s files...' % args[0]
        return 2

    if len(args[1:]) > 1:
        for filename in args[1:]:
            print "File :", filename
            Discoverer(filename).print_info()
            print "\n"
    else:
        Discoverer(args[1]).print_info()

if __name__ == '__main__':
    sys.exit(main(sys.argv))
