#!/usr/bin/env python

# gst-file.py
# (c) 2005 Edward Hervey <edward at fluendo dot com>
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

    def __init__(self, filename):
        gobject.GObject.__init__(self)

        self.mimetype = None

        if not os.path.isfile(filename):
            self.finished = True
            return
        
        self.finished = False
        # caps are quick hack, we should in fact store that information
        # in separate variables (videowidth, videoheight, audiorate,...)
        self.audiocaps = {}
        self.videocaps = {}
        # The length queried on the audio pad and the video pad can be different
        self.audiolength = 0L
        self.videolength = 0L
        self.is_video = False
        self.is_audio = False

        # The tags we get when processing
        self.tags = {}

        # the initial elements of the pipeline
        self.src = gst.element_factory_make("filesrc")
        self.src.set_property("location", filename)
        self.dbin = gst.element_factory_make("decodebin")
        self.add_many(self.src, self.dbin)
        self.src.link(self.dbin)
        self.typefind = self.dbin.get_by_name("typefind")

        # callbacks
        self.typefind.connect("have-type", self._have_type_cb)
        self.dbin.connect("new-decoded-pad", self._new_decoded_pad_cb)
        self.dbin.connect("found-tag", self._found_tag_cb)

    def discover(self):
        """iterate on ourself to find the information on the given file"""
        if self.finished:
            return
        self.set_state(gst.STATE_PLAYING)
        while 1:
            if not self.iterate():
                break
        self.set_state(gst.STATE_NULL)

    def print_info(self):
        """prints out the information on the given file"""
        if not self.finished:
            self.discover()
        if not self.mimetype:
            print "Unknow media type"
            return
        print "Mime Type :\t", self.mimetype
        if not self.is_video and not self.is_audio:
            return
        print "Length :\t", time_to_string(max(self.audiolength, self.videolength))
        if self.is_video:
            print "Video :"
            print "\t%d x %d @ %.2f fps" % (self.videocaps[0]["width"],
                                            self.videocaps[0]["height"],
                                            self.videocaps[0]["framerate"])
            if self.tags.has_key("video-codec"):
                print "\tCodec :", self.tags.pop("video-codec")
        if self.is_audio:
            print "Audio :"
            if self.tags.has_key("audio-codec"):
                print "\tCodec :", self.tags.pop("audio-codec")
        if self.tags:
            print "Additional information :"
            for tag in self.tags.keys():
                print "%20s :\t" % tag, self.tags[tag]

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
            if (not self.is_video) or self.videocaps:
                self.finished = True
        else:
            self.videocaps = caps
            self.videolength = length
            if (not self.is_audio) or self.audiocaps:
                self.finished = True

    def _new_decoded_pad_cb(self, dbin, pad, is_last):
        # Does the file contain got audio or video ?
        if "audio" in pad.get_caps().to_string():
            self.is_audio = True
        elif "video" in pad.get_caps().to_string():
            self.is_video = True
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
