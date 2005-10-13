#!/usr/bin/env python

# gstfile.py
# (c) 2005 Edward Hervey <edward at fluendo dot com>
# Discovers and prints out multimedia information of files

# This example shows how to use gst-python:
# _ in an object-oriented way (Discoverer class)
# _ subclassing a gst.Pipeline
# _ and overidding existing methods (do_iterate())

import os
import sys

import gobject

import pygst
pygst.require('0.9')

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
        self.add(self.src, self.dbin)
        self.src.link(self.dbin)
        self.typefind = self.dbin.get_by_name("typefind")

        # callbacks
        self.typefind.connect("have-type", self._have_type_cb)
        self.dbin.connect("new-decoded-pad", self._new_decoded_pad_cb)
        self.dbin.connect("unknown-type", self._unknown_type_cb)
        #self.dbin.connect("found-tag", self._found_tag_cb)

        self.discover()

    def discover(self):
        """iterate on ourself to find the information on the given file"""
        if self.finished:
            return
        self.info("setting to PLAY")
        if not self.set_state(gst.STATE_PLAYING):
            # the pipeline wasn't able to be set to playing
            self.finished = True
            return
        bus = self.get_bus()
        while 1:
            if self.finished:
                self.debug("self.finished, stopping")
                break
            msg = bus.poll(gst.MESSAGE_ANY, gst.SECOND)
            if not msg:
                print "got empty message..."
                break
            #print "##", msg.type
            if msg.type & gst.MESSAGE_STATE_CHANGED:
                #print "## state changed\t", msg.src.get_name() ,
                #print msg.parse_state_changed()
                pass
            elif msg.type & gst.MESSAGE_EOS:
                #print "EOS"
                break
            elif msg.type & gst.MESSAGE_TAG:
                for key in msg.parse_tag().keys():
                    self.tags[key] = msg.structure[key]
                #print msg.structure.to_string()
            elif msg.type & gst.MESSAGE_ERROR:
                print "whooops, error", msg.parse_error()
                break
            else:
                print "unknown message type"
                
#       self.info( "going to PAUSED")
        self.set_state(gst.STATE_PAUSED)
#       self.info("going to ready")
        self.set_state(gst.STATE_READY)
#        print "now in ready"
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
        print "unknown type", caps.to_string()
        # if we get an unknown type and we don't already have an
        # audio or video pad, we are finished !
        self.otherstreams.append(caps.to_string())
        if not self.is_video and not self.is_audio:
            self.finished = True

    def _have_type_cb(self, typefind, prob, caps):
        self.mimetype = caps.to_string()

    def _notify_caps_cb(self, pad, args):
        caps = pad.get_negotiated_caps()
#        print "caps notify on", pad, ":", caps
        if not caps:
            return
        # the caps are fixed
        # We now get the total length of that stream
        q = gst.query_new_position(gst.FORMAT_TIME)
        #print "query refcount", q.__grefcount__
        pad.info("sending position query")
        if pad.get_peer().query(q):
            #print "query refcount", q.__grefcount__
            length = q.structure["end"]
            pos = q.structure["cur"]
            format = q.structure["format"]
            pad.info("got position query answer : %d:%d:%d" % (length, pos, format))
            #print "got length", time_to_string(pos), time_to_string(length), format
        else:
            gst.warning("position query didn't work")
        #length = pad.get_peer().query(gst.QUERY_TOTAL, gst.FORMAT_TIME)
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
        caps = pad.get_caps()
        gst.info("caps:%s" % caps.to_string())
#        print "new decoded pad", caps.to_string()
        if "audio" in caps.to_string():
            self.is_audio = True
##             if caps.is_fixed():
##                 print "have negotiated caps", caps
##                 self.audiocaps = caps
##                 return
        elif "video" in caps.to_string():
            self.is_video = True
##             if caps.is_fixed():
##                 print "have negotiated caps", caps
##                 self.videocaps = caps
##                 return
        else:
            print "got a different caps..", caps
            return
        if is_last and not self.is_video and not self.is_audio:
            self.finished = True
            return
        # we connect a fakesink to the new pad...
        pad.info("adding queue->fakesink")
        fakesink = gst.element_factory_make("fakesink")
        queue = gst.element_factory_make("queue")
        self.add(fakesink, queue)
        queue.link(fakesink)
        sinkpad = fakesink.get_pad("sink")
        queuepad = queue.get_pad("sink")
        # ... and connect a callback for when the caps are fixed
        sinkpad.connect("notify::caps", self._notify_caps_cb)
        if pad.link(queuepad):
            pad.warning("##### Couldn't link pad to queue")
        queue.set_state(gst.STATE_PLAYING)
        fakesink.set_state(gst.STATE_PLAYING)
        gst.info('finished here')

    def _found_tag_cb(self, dbin, source, tags):
        self.tags.update(tags)

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
