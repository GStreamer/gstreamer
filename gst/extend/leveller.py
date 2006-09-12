# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
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
import math

import gobject
import pygst
pygst.require('0.10')
import gst

import utils
from pygobject import gsignal

import sources
from sources import EOS, ERROR, UNKNOWN_TYPE, WRONG_TYPE

class Leveller(gst.Pipeline):
    """
    I am a pipeline that calculates RMS values and mix-in/out points.
    I will signal 'done' when I'm done scanning the file, with return value
    EOS, ERROR, UNKNOWN_TYPE, or WRONG_TYPE from gst.extend.sources
    """

    gsignal('done', str)

    def __init__(self, filename, threshold=-9.0):
        gst.Pipeline.__init__(self)

        self._filename = filename

        self._thresholddB = threshold
        self._threshold = math.pow(10, self._thresholddB / 10.0)

        self._source = sources.AudioSource(filename)
        self._source.connect('done', self._done_cb)

        self._level = gst.element_factory_make("level")

        self._fakesink = gst.element_factory_make("fakesink")

        self.add(self._source, self._level, self._fakesink)
        self._source.connect("pad-added", self._sourcePadAddedCb)
        self._level.link(self._fakesink)

        # temporary values for each timepoint
        self._rmsdB = {} # hash of channel, rmsdB value
        self._peakdB = 0.0 # highest value over all channels for this time

        # results over the whole file
        self._meansquaresums = [] # list of time -> mean square sum value
        self._peaksdB = [] # list of time -> peak value

        self._lasttime = 0

        # will be set when done
        self.mixin = 0
        self.mixout = 0
        self.length = 0
        self.rms = 0.0
        self.rmsdB = 0.0

    def _sourcePadAddedCb(self, source, pad):
        self._source.link(self._level)

    def do_handle_message(self, message):
        self.debug("got message %r" % message)
        if (message.type == gst.MESSAGE_ELEMENT) and (message.src == self._level):
            struc = message.structure
            endtime = struc["endtime"]
            rmss = struc["rms"]
            peaks = struc["peak"]
            decays = struc["decay"]
            infos = zip(rmss, peaks, decays)
            channid = 0
            for rms,peak,decay in infos:
                self._level_cb(message.src, endtime, channid, rms, peak, decay)
                channid += 1
        elif message.type == gst.MESSAGE_EOS:
            self._eos_cb(message.src)
        # chaining up 
        gst.Pipeline.do_handle_message(self, message)

    def _level_cb(self, element, time, channel, rmsdB, peakdB, decaydB):
        # rms is being signalled in dB
        # FIXME: maybe level should have a way of signalling actual values
        # signals are received in order, so I should get each channel one
        # by one
        if time > self._lasttime and self._lasttime > 0:
            # we have a new time point, so calculate stuff for the old block
            meansquaresum = 0.0
            for i in self._rmsdB.keys():
                meansquaresum += math.pow(10, self._rmsdB[i] / 10.0)
            # average over channels
            meansquaresum /= len(self._rmsdB.keys())
            try:
                rmsdBstr = str(10 * math.log10(meansquaresum))
            except OverflowError:
                rmsdBstr = "(-inf)"
            gst.log("meansquaresum %f (%s dB)" % (meansquaresum, rmsdBstr))

            # update values
            self._peaksdB.append((self._lasttime, peakdB))
            self._meansquaresums.append((self._lasttime, meansquaresum))
            self._rmsdB = {}
            self._peakdB = 0.0

        # store the current values for later processing
        gst.log("time %s, channel %d, rmsdB %f" % (gst.TIME_ARGS(time), channel, rmsdB))
        self._lasttime = time
        self._rmsdB[channel] = rmsdB
        if peakdB > self._peakdB:
            self._peakdB = peakdB

    def _done_cb(self, source, reason):
        gst.debug("done, reason %s" % reason)
        # we ignore eos because we want the whole pipeline to eos
        if reason == EOS:
            return
        self.emit('done', reason)

    def _eos_cb(self, source):
        gst.debug("eos, start calcing")

        # get the highest peak RMS for this track
        highestdB = self._peaksdB[0][1]

        for (time, peakdB) in self._peaksdB:
            if peakdB > highestdB:
                highestdB = peakdB
        gst.debug("highest peak(dB): %f" % highestdB)

        # get the length
        (self.length, peakdB) = self._peaksdB[-1]
        
        # find the mix in point
        for (time, peakdB) in self._peaksdB:
            gst.log("time %s, peakdB %f" % (gst.TIME_ARGS(time), peakdB))
            if peakdB > self._thresholddB + highestdB:
                gst.debug("found mix-in point of %f dB at %s" % (
                    peakdB, gst.TIME_ARGS(time)))
                self.mixin = time
                break

        # reverse and find out point
        self._peaksdB.reverse()
        found = None
        for (time, peakdB) in self._peaksdB:
            if found:
                self.mixout = time
                gst.debug("found mix-out point of %f dB right before %s" % (
                    found, gst.TIME_ARGS(time)))
                break
                
            if peakdB > self._thresholddB + highestdB:
                found = peakdB

        # now calculate RMS between these two points
        weightedsquaresums = 0.0
        lasttime = self.mixin
        for (time, meansquaresum) in self._meansquaresums:
            if time <= self.mixin:
                continue

            delta = time - lasttime
            weightedsquaresums += meansquaresum * delta
            gst.log("added MSS %f over time %s at time %s, now %f" % (
                meansquaresum, gst.TIME_ARGS(delta),
                gst.TIME_ARGS(time), weightedsquaresums))

            lasttime = time
            
            if time > self.mixout:
                break

        # calculate
        try:
            ms = weightedsquaresums / (self.mixout - self.mixin)
        except ZeroDivisionError:
            # this is possible when, for example, the whole sound file is
            # empty
            gst.warning('ZeroDivisionError on %s, mixin %s, mixout %s' % (
                self._filename, gst.TIME_ARGS(self.mixin),
                gst.TIME_ARGS(self.mixout)))
            self.emit('done', WRONG_TYPE)
            return

        self.rms = math.sqrt(ms)
        self.rmsdB = 10 * math.log10(ms)

        self.emit('done', EOS)

    def start(self):
        gst.debug("Setting to PLAYING")
        self.set_state(gst.STATE_PLAYING)
        gst.debug("Set to PLAYING")

    # FIXME: we might want to do this ourselves automatically ?
    def stop(self):
        """
        Stop the leveller, freeing all resources.
        Call after the leveller emitted 'done' to clean up.
        """
        gst.debug("Setting to NULL")
        self.set_state(gst.STATE_NULL)
        gst.debug("Set to NULL")
        utils.gc_collect('Leveller.stop()')

    def clean(self):
        # clean ourselves up completely
        self.stop()
        # let's be ghetto and clean out our bin manually
        self.remove(self._source)
        self.remove(self._level)
        self.remove(self._fakesink)
        gst.debug("Emptied myself")
        self._source.clean()
        utils.gc_collect('Leveller.clean() cleaned up source')
        self._source = None
        self._fakesink = None
        self._level = None
        utils.gc_collect('Leveller.clean() done')

gobject.type_register(Leveller)

if __name__ == "__main__":
    main = gobject.MainLoop()

    try:
        leveller = Leveller(sys.argv[1])
    except IndexError:
        sys.stderr.write("Please give a file to calculate level of\n")
        sys.exit(1)

    print "Starting"
    bus = leveller.get_bus()
    bus.add_signal_watch()
    dontstop = True

    leveller.set_state(gst.STATE_PLAYING)
    
    while dontstop:
        message = bus.poll(gst.MESSAGE_ANY, gst.SECOND)
        if message:
            gst.debug("got message from poll:%s/%r" % (message.type, message))
        else:
            gst.debug("got NOTHING from poll")
        if message:
            if message.type == gst.MESSAGE_EOS:
                print "in: %s, out: %s, length: %s" % (gst.TIME_ARGS(leveller.mixin),
                                                       gst.TIME_ARGS(leveller.mixout),
                                                       gst.TIME_ARGS(leveller.length))
                print "rms: %f, %f dB" % (leveller.rms, leveller.rmsdB)
                dontstop = False
            elif message.type == gst.MESSAGE_ERROR:
                error,debug = message.parse_error()
                print "ERROR[%s] %s" % (error.domain, error.message)
                dontstop = False

    leveller.stop()
    leveller.clean()

    gst.debug('deleting leveller, verify objects are freed')
    utils.gc_collect('quit main loop')
    del leveller
    utils.gc_collect('deleted leveller')
    gst.debug('stopping forever')
