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
gobject.threads_init()
import pygst
pygst.require('0.10')
import gst

import utils
from pygobject import gsignal
import sources
from leveller import Leveller

class Jukebox(gst.Bin):
    gsignal('done', str)
    gsignal('prerolled')        # emitted when at least 2 sources are ready
    gsignal('changed', str, gobject.TYPE_UINT64) # clocktime, filename
    gsignal('looped')

    def __init__(self, files, rms=0.2, loops=0, random=False,
                 caps="audio/x-raw-int,channels=2,rate=44100",
                 picklepath='level.pck'):
        # with pygtk 2.4 this call is needed for the gsignal to work
        self.__gobject_init__()

        self._target_rms = rms
        self._loopsleft = loops
        self._loopsdone = 0
        self._random = random
        self._picklepath = picklepath
        self._caps = gst.caps_from_string(caps)
        self._files = files[:] # copy
        self._levels = {} # filename -> rms, mixin, mixout, length
        self._prerolled = False
        self._playing = False
        self._scani = 0 # index into self._files for scanning
        self._playi = 0 # index into self._files for playing

        self._lastadded = None # last file added to composition
        self._lastposition = long(0) # last position where file was added

        if not len(files) > 1:
            raise TypeError, 'Must have at least 2 files'

        self._composition = gst.element_factory_make("gnlcomposition")
        self._composition.connect('pad-added', self._composition_pad_added_cb)
        self.add(self._composition)

        self._srcpad = None

        # load our pickle if it exists
        if os.path.exists(self._picklepath):
            file = open(self._picklepath)
            self._levels = pickle.load(file)
            file.close()

        # randomize our list if asked for
        if self._random:
            self._files = rand.sample(self._files, len(self._files))


    ## public API

    def preroll(self):
        # scan the first few files and start playing
        gst.debug("starting jukebox prerolling")
        self._scan()

    def start(self):
        ##
        ## FIXME : THIS SHOULD'T BE NEEDED !
        ## USE STATE CHANGES INSTEAD
        ## 
        if not self._prerolled:
            raise Exception, "baby"
        self.set_state(gst.STATE_PAUSED)


    ## Scanning private methods

    def _scan(self):
        # start a leveller for a new _toscan file
        if self._scani >= len(self._files):
            gst.debug("We're done scanning !")
            return

        file = self._files[self._scani]
        self._scani += 1

        if file in self._levels.keys():
            gst.debug("already did file %s" % file)
            self._check_prerolled()
            gobject.timeout_add(0, self._scan)
            return

        gst.debug("creating leveller for %s" % file)
        leveller = Leveller(file)
        leveller.connect('done', self._leveller_done_cb, file)
        gobject.timeout_add(0, leveller.start)
        ##gobject.idle_add(leveller.iterate)

    def _leveller_done_cb(self, l, reason, file):
        if reason != sources.EOS:
            gst.debug("Error: %s" % reason)
            return

        gst.debug("in: %s, out: %s" % (gst.TIME_ARGS(l.mixin),
                                       gst.TIME_ARGS(l.mixout)))
        gst.debug("rms: %f, %f dB" % (l.rms, l.rmsdB))

        # store infos
        self._levels[file] = (l.rms, l.mixin, l.mixout, l.length)

        gst.debug("writing level pickle")
        file = open(self._picklepath, "w")
        pickle.dump(self._levels, file)
        file.close()

        self._check_prerolled()
        self._scan()

        # clean up leveller after this handler
        gobject.timeout_add(0, l.clean)


    ## GnlSource-related methods

    def _new_gnl_source(self, location, start):
        """
        Creates a new GnlSource containing an AudioSource with the following
        properties correctly set:
        _ volume level
        _ priority
        _ duration
        The start position MUST be given
        """
        if not self._levels[location]:
            return None
        self.debug("Creating new GnlSource at %s for %s" % (gst.TIME_ARGS(start), location))
        idx = self._files.index(location) + self._loopsdone * len(self._files)
        rms, mixin, mixout, duration = self._levels[location]
        gnls = gst.element_factory_make("gnlsource", "source-%d-%s" % (idx, location))
        src = sources.AudioSource(location)
        gnls.add(src)

        # set volume
        level = 1.0
        if rms > self._target_rms:
            level = self._target_rms / rms
            gst.debug('setting volume of %f' % level)
        else:
            gst.debug('not going to go above 1.0 level')
        src.set_volume(level)

        # set proper position/duration/priority in composition
        gnls.props.priority = (2 * self._loopsdone) + 1 + (idx % 2)
        gnls.props.start = long(start)
        gnls.props.duration = long(duration)
        gnls.props.media_duration = long(duration)
        gnls.props.media_start = long(0)

        return gnls

    def _new_mixer(self, start, duration):
        gnlo = gst.element_factory_make("gnloperation")
        ad = gst.element_factory_make("adder")
        gnlo.add(ad)
        gnlo.props.sinks = 2
        gnlo.props.start = start
        gnlo.props.duration = duration
        gnlo.props.priority = 0

        return gnlo

    def _append_file(self, location):
        """
        Appends the given file to the composition, along with the proper mixer effect
        """
        self.debug("location:%s" % location)
        start = self._lastposition
        if self._lastadded:
            start += self._levels[self._lastadded][2]
            start -= self._levels[location][1]

        gnls = self._new_gnl_source(location, start)
        self._composition.add(gnls)

        if self._lastadded:
            # create the mixer
            duration = self._levels[self._lastadded][3] - self._levels[self._lastadded][2] + self._levels[location][1]
            mixer = self._new_mixer(start, duration)
            self._composition.add(mixer)

        self._lastposition = start
        self._lastadded = location

        self.debug("lastposition:%s , lastadded:%s" % (gst.TIME_ARGS(self._lastposition),
                                                       self._lastadded))

    def _check_prerolled(self):
        gst.debug("_check_prerolled: index: scan %d, play %d" % (
            self._scani, self._playi))
        if not self._prerolled and self._scani > self._playi + 1:
            self._prerolled = True
            # add initial sources here
            self._append_file(self._files[0])
            self._append_file(self._files[1])
            self.debug("now prerolled and ready to play")
            self.emit('prerolled')


    def _emit_changed(self, file, when):
        print "emitting changed for %s at %r" % (file, when)
        self.emit('changed', file, when)

    def _source_clean(self, source):
        source.set_state(gst.STATE_NULL)
        self.remove(source)
        source.clean()

    ## composition callbacks

    def _composition_pad_added_cb(self, comp, pad):
        if self._srcpad:
            return
        self.debug("Ghosting source pad %s" % pad)
        self._srcpad = gst.GhostPad("src", pad)
        self._srcpad.set_active(True)
        self.add_pad(self._srcpad)

    ## gst.Bin/Element virtual methods

    def do_handle_message(self, message):
        self.debug("got message %s / %s / %r" % (message.src.get_name(), message.type.first_value_name, message))

        # chaining up
        gst.Bin.do_handle_message(self, message)

    def do_state_change(self, transition):
        if not self._prerolled:
            gst.error("Call Jukebox.preroll() before!")
            return gst.STATE_CHANGE_FAILURE
        # chaining up
        return gst.Bin.do_state_change(self, message)

gobject.type_register(Jukebox)

# helper functions
def _find_elements_recurse(element):
    if not isinstance(element, gst.Bin):
        return [element, ]
    l = []
    for e in element.elements():
        l.extend(_find_elements_recurse(e))
    return l

def _find_unconnected_pad(bin, direction):
    for e in _find_elements_recurse(bin):
        for p in e.pads():
            if p.get_direction() == direction and not p.get_peer():
                return p

    return None

# run us to test
if __name__ == "__main__":
    main = gobject.MainLoop()
    pipeline = gst.Pipeline('jukebox')
    list = open(sys.argv[1]).read().rstrip().split('\n')
    print list
    #source = Jukebox(list, random=True, loops=-1)
    source = Jukebox(list, random=True, loops=1)

    def _jukebox_prerolled_cb(jukebox):
        print "prerolled"
        _start()

    def _jukebox_changed_cb(jukebox, filename, when):
        print "changed file to %s at %s" % (filename, float(when) / gst.TIME_ARGS(gst.SECOND))

    def _jukebox_looped_cb(jukebox):
        print "jukebox looped"

    def _start():
        source.start()
        print "setting pipeline to PLAYING"
        pipeline.set_state(gst.STATE_PLAYING)
        print "set pipeline to PLAYING"

    def jukebox_pad_added(comp, pad, sinkpad):
        pad.link(sinkpad)

    def jukebox_message(bus, message):
        if message.type == gst.MESSAGE_ERROR:
            print "Error: %s" % message.parse_error()
            main.quit()
        elif message.type == gst.MESSAGE_EOS:
            print "done"
            main.quit()

    source.connect('prerolled', _jukebox_prerolled_cb)
    source.connect('changed', _jukebox_changed_cb)
    source.connect('looped', _jukebox_looped_cb)
    source.preroll()
    pipeline.add(source)

    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", jukebox_message)

    p = "alsasink"
    if len(sys.argv) > 2:
        p = " ".join(sys.argv[2:])

    print "parsing output pipeline %s" % p
    sinkbin = gst.parse_launch("bin.( %s )" % p)
    pipeline.add(sinkbin)
    apad = _find_unconnected_pad(sinkbin, gst.PAD_SINK)
    if not apad:
        raise TypeError, "No unconnected sink pad found in bin %r" % sinkbin
    sinkpad = gst.GhostPad("sink", apad)
    sinkbin.add_pad(sinkpad)
    source.connect('pad-added', jukebox_pad_added, sinkpad)

    print "Going into main loop"
    sys.stdout.flush()
    main.run()
    print "Left main loop"
    sys.stdout.flush()

    pipeline.set_state(gst.STATE_NULL)
