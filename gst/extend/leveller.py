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
import gst

from pygobject import gsignal

from gst.extend import sources
from gst.extend.sources import EOS, ERROR, UNKNOWN_TYPE, WRONG_TYPE

class Leveller(gst.Pipeline):
    """
    I am a pipeline that calculates RMS values and mix-in/out points.
    I will signal 'done' when I'm done scanning the file, with return value
    EOS, ERROR, UNKNOWN_TYPE, or WRONG_TYPE from gst.extend.sources
    """

    gsignal('done', str)

    def __init__(self, filename, threshold=-9.0):
        self.__gobject_init__()

        self._thresholddB = threshold
        self._threshold = math.pow(10, self._thresholddB / 10.0)

        self._source = sources.AudioSource(filename)
        self._source.connect('eos', self._eos_cb)
        self._source.connect('done', self._done_cb)

        self._level = gst.element_factory_make("level")
        self._level.set_property("signal", True)
        self._level.connect("level", self._level_cb)

        self._fakesink = gst.element_factory_make("fakesink")
        # FIXME: make this syntax possible
        #self.add(self._source, self._level, self._fakesink)
        self.add(self._source)
        self.add(self._level)
        self.add(self._fakesink)
        self._source.link(self._level)
        self._level.link(self._fakesink)

        # temporary values for each timepoint
        self._rmsdB = {} # hash of channel, rmsdB value
        self._peakdB = 0.0 # highest value over all channels for this time

        # results over the whole file
        self._meansquaresums = [] # list of time -> mean square sum value
        self._peaksdB = [] # list of time -> peak value

        self._lasttime = 0.0

        # will be set when done
        self.mixin = 0.0
        self.mixout = 0.0
        self.length = 0.0
        self.rms = 0.0
        self.rmsdB = 0.0

    def _level_cb(self, element, time, channel, rmsdB, peakdB, decaydB):
        # rms is being signalled in dB
        # FIXME: maybe level should have a way of signalling actual values
        # signals are received in order, so I should get each channel one
        # by one
        if time > self._lasttime and self._lasttime > 0.0:
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
        gst.log("time %f, channel %d, rmsdB %f" % (time, channel, rmsdB))
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
            gst.log("time %f, peakdB %f" % (time, peakdB))
            if peakdB > self._thresholddB + highestdB:
                gst.debug("found mix-in point of %f dB at %f" % (
                    peakdB, time))
                self.mixin = time
                break

        # reverse and find out point
        self._peaksdB.reverse()
        found = None
        for (time, peakdB) in self._peaksdB:
            if found:
                self.mixout = time
                gst.debug("found mix-out point of %f dB right before %f" % (
                    found, time))
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
            gst.log("added MSS %f over time %f at time %f, now %f" % (
                meansquaresum, delta, time, weightedsquaresums))

            lasttime = time
            
            if time > self.mixout:
                break

        # calculate
        ms = weightedsquaresums / (self.mixout - self.mixin)
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

gobject.type_register(Leveller)

if __name__ == "__main__":
    main = gobject.MainLoop()

    def _done_cb(leveller, reason, l):
        print "Done"
        if reason != EOS:
            print "Error: %s" % reason
        else:
            print "in: %f, out: %f, length: %f" % (l.mixin, l.mixout, l.length)
            print "rms: %f, %f dB" % (l.rms, l.rmsdB)
        leveller.stop()
        main.quit()

    def _error_cb(leveller, source, gerror, message):
        print "Error: %s" % gerror
        main.quit()

    try:
        leveller = Leveller(sys.argv[1])
    except IndexError:
        sys.stderr.write("Please give a file to calculate level of")
        sys.exit(1)

    leveller.connect('done', _done_cb, leveller)
    leveller.connect('error', _error_cb)

    gobject.timeout_add(0, leveller.start)
    gobject.idle_add(leveller.iterate)

    print "Starting"
    main.run()
