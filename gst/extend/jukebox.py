#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2005 Edward Hervey <edward at fluendo dot com>
# Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>

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
import pickle
import random as rand

import gobject
import gst

import utils
from pygobject import gsignal
import sources
from leveller import Leveller

class Jukebox(gst.Bin):
    gsignal('done', str)
    gsignal('prerolled')

    def __init__(self, files, rms=0.1, loops=0, random=False,
                 caps="audio/x-raw-int,channels=2,rate=44100",
                 picklepath='level.pck'):
        gst.Bin.__init__(self)
        # with pygtk 2.4 this call is needed for the gsignal to work
        self.__gobject_init__()

        self._target_rms = rms
        self._loopsleft = loops
        self._random = random
        self._picklepath = picklepath
        self._caps = gst.caps_from_string(caps)
        self._files = files[:] # copy
        self._levels = {} # filename -> rms, mixin, mixout
        self._prerolled = False
        self._playing = False
        self._triggers = [] # list of (time, callable, *args)
        self._scani = 0 # index into self._files for scanning
        self._playi = 0 # index into self._files for playing
        self._next_mix = 0.0 # time of the next mix point

        if not len(files) > 1:
            raise TypeError, 'Must have at least 2 files'

        # load our pickle if it exists
        if os.path.exists(self._picklepath):
            file = open(self._picklepath)
            self._levels = pickle.load(file)
            file.close()

        # FIXME: randomize our list if asked for
        if self._random:
            self._files = rand.sample(self._files, len(self._files))

        # our ghost pad we feed from
        self._adder = gst.element_factory_make("adder")
        self.add(self._adder)
        self.add_ghost_pad(self._adder.get_pad("src"), "src")
        probe = gst.Probe(False, self._probe_cb)
        self._adder.get_pad("src").add_probe(probe)

    def log(self, *args):
        print " ".join(args)
        sys.stdout.flush()
        pass

    def _probe_cb(self, probe, buffer):
        if not self._triggers:
            return True
        now = float(buffer.timestamp) / gst.SECOND
        if buffer.duration is not gst.CLOCK_TIME_NONE:
            now += float(buffer.duration) / gst.SECOND
            
        next = self._triggers[0][0]
        if now > next:
            self.log("now %f, next %f" % (now, next))
            self.log("running trigger")
            t = self._triggers.pop()
            method = t[1]
            args = t[2:] 
            method(*args)

        return True

    def _scan(self):
        # start a leveller for a new _toscan file
        if self._scani >= len(self._files):
            self.log("We're done scanning !")
            return

        file = self._files[self._scani]
        self._scani += 1

        if file in self._levels.keys():
            self.log("already did file %s" % file)
            self._check_prerolled()
            gobject.timeout_add(0, self._scan)
            return
            
        self.log("creating leveller for %s" % file)
        leveller = Leveller(file)
        leveller.connect('done', self._leveller_done_cb, file)
        gobject.timeout_add(0, leveller.start)
        gobject.idle_add(leveller.iterate)

    def _leveller_done_cb(self, l, reason, file):
        if reason != sources.EOS:
            self.log("Error: %s" % reason)
            return

        self.log("in: %f, out: %f" % (l.mixin, l.mixout))
        self.log("rms: %f, %f dB" % (l.rms, l.rmsdB))
        self._levels[file] = (l.rms, l.mixin, l.mixout, l.length)
        self.log("writing level pickle")
        file = open(self._picklepath, "w")
        pickle.dump(self._levels, file)
        file.close()

        self._check_prerolled()
        self._scan()

    def _check_prerolled(self):
        self.log("_check_prerolled: index: scan %d, play %d" % (
            self._scani, self._playi))
        if not self._prerolled and self._scani > self._playi + 1:
            self._prerolled = True
            self.emit('prerolled')

    def preroll(self):
        # scan the first few files and start playing
        self.log("starting jukebox prerolling")
        self._scan()

    def start(self):
        if not self._prerolled:
            raise Exception, "baby"
        self.log("START of mixing")
        file = self._get_next_play()
        (rms, mixin, mixout, length) = self._levels[file]
        self.log("START with %s (%f/%f/%f)" % (file, mixin, mixout, length))
        self._next_mix = mixout
        self._source_play(file)
        
    def _schedule_next_play(self):
        file = self._get_next_play()
        if not file:
            self.log("No more files left to schedule for play")
            return

        try:
            (rms, mixin, mixout, length) = self._levels[file]
        except IndexError:
            raise AssertionError, "file %s not in self._levels"

        when = self._next_mix - mixin
        self.log("Scheduling start of %s at %f" % (file, when))
        self._triggers.append((when, self._source_play, file))

        self._next_mix = when + mixout
        self.log("Next mix should happen at %f" % when)

    def _peek_next_play(self):
        """
        Look at what the next scheduled play is
        """
        return self._files[self._playi]

    def _get_next_play(self):
        """
        Return the next file to play, possibly re-randomizing the playlist
        if we go back to the top
        """
        if self._playi >= len(self._files):
            if self._loopsleft == 0:
                # we're going to be done, emit after all pads on adder are gone
                return None
                
            self.log("Reset play pointer to top")
            self._loopsleft -= 1
            if self._random:
                self.log("Reshuffling")
                self._files = rand.sample(self._files, len(self._files))
            self._playi = 0

        file = self._files[self._playi]
        self._playi += 1

        self.log("Returning next play file %s" % file)
        return file
        
    def _source_play(self, file):
        # take the next file, and connect it to adder
        self.log('_source_play: prerolling %s' % file)
        source = sources.AudioSource(file)
        self.add(source)
        source.connect('prerolled', self._source_prerolled_cb)
        source.connect('done', self._source_done_cb)
        source.set_state(gst.STATE_PLAYING)

    def _source_prerolled_cb(self, source):
        self.log('source %r prerolled' % source)
        srcpad = source.get_pad("src")
        sinkpad = self._adder.get_request_pad("sink%d")
        self.log("Linking srcpad %r to adder pad %r using caps %r" % (
            srcpad, sinkpad, self._caps))
        srcpad.link_filtered(sinkpad, self._caps)
        self._schedule_next_play()

    def _source_done_cb(self, source, reason):
        self.log('source %r done' % source)
        srcpad = source.get_pad("src")
        sinkpad = srcpad.get_peer()
        srcpad.unlink(sinkpad)
        self._adder.release_request_pad(sinkpad)

        if len(self._adder.get_pad_list()) == 1:
            self.log('only a source pad left, so we are done')
            self.emit('done', sources.EOS)
        
gobject.type_register(Jukebox)
        
# helper functions
def _find_elements_recurse(element):
    if not isinstance(element, gst.Bin):
        return [element, ]
    l = []
    for e in element.get_list():
        l.extend(_find_elements_recurse(e))
    return l

def _find_unconnected_pad(bin, direction):
    for e in _find_elements_recurse(bin):
        for p in e.get_pad_list():
            if p.get_direction() == direction and not p.get_peer():
                return p

    return None

# run us to test
if __name__ == "__main__":
    main = gobject.MainLoop()
    pipeline = gst.Pipeline('jukebox')
    list = open(sys.argv[1]).read().rstrip().split('\n')
    print list
    source = Jukebox(list, random=True, loops=-1)

    def _done_cb(source, reason):
        print "Done"
        if reason != EOS:
            print "Some error happened: %s" % reason
        main.quit()

    def _error_cb(source, element, gerror, message):
        print "Error: %s" % gerror
        main.quit()
        
    def _jukebox_prerolled(jukebox):
        print "prerolled"
        _start()

    def _jukebox_done(jukebox, reason):
        print "done"
        main.quit()
        
    def _iterate_idler():
        #print "iterating pipeline"
        #sys.stdout.flush()
        #utils.gst_dump(pipeline)
        ret = pipeline.iterate()
        #print "iterated pipeline, %d" % ret
        #sys.stdout.flush()
        return ret

    def _error_cb(element, source, gerror, message):
        print "Error: %s" % gerror
        main.quit()

    def _start():
        source.start()
        print "setting pipeline to PLAYING"
        pipeline.set_state(gst.STATE_PLAYING)
        print "set pipeline to PLAYING"
        gobject.idle_add(_iterate_idler)

    source.connect('prerolled', _jukebox_prerolled)
    source.connect('done', _jukebox_done)
    source.preroll()
    pipeline.add(source)

    p = "osssink"
    if len(sys.argv) > 2:
        p = " ".join(sys.argv[2:])
    
    print "parsing output pipeline %s" % p
    sinkbin = gst.parse_launch("bin.( %s )" % p)
    pipeline.add(sinkbin)
    sinkpad = _find_unconnected_pad(sinkbin, gst.PAD_SINK)
    if not sinkpad:
        raise TypeError, "No unconnected sink pad found in bin %r" % sinkbin
    srcpad = source.get_pad("src")
    srcpad.link_filtered(sinkpad,
        gst.caps_from_string("audio/x-raw-int,channels=2,rate=44100,width=16,depth=16"))
    pipeline.connect('error', _error_cb)

    print "Going into main loop"
    sys.stdout.flush()
    main.run()
    print "Left main loop"
    sys.stdout.flush()

    pipeline.set_state(gst.STATE_NULL)
