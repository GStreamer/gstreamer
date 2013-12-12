#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# gst-python
# Copyright (C) 2003 David I. Lehn
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>
#

import pygtk
pygtk.require('2.0')

import sys
import time
import gobject
import gtk

import pygst
pygst.require('0.10')

import gst

class BPS(object):
    def __init__(self):
        self.buffers = 0
        self.start = 0

    def done(self):
        end = time.time()
        dt = end - self.start
        bps = self.buffers/dt
        spb = dt/self.buffers
        print '\t%d buffers / %fs\t= %f bps\t= %f spb' % (self.buffers, dt, bps, spb)

    def fakesrc(self, buffers):
        src = gst.element_factory_make('fakesrc','src')
        src.set_property('silent', 1)
        src.set_property('num_buffers', buffers)
        return src

    def fakesink(self):
        sink = gst.element_factory_make('fakesink','sink')
        sink.set_property('silent', 1)
        return sink

    def build_pipeline(self, buffers):
        pipeline = gst.Pipeline('pipeline')

        src = self.fakesrc(buffers)
        pipeline.add(src)
        sink = self.fakesink()
        pipeline.add(sink)
        src.link(sink)

        return pipeline

    def idle(self, pipeline):
        return pipeline.iterate()

    def test(self):
        self.bus = self.pipeline.get_bus()

        self.start = time.time()
        
        self.pipeline.set_state(gst.STATE_PLAYING)

        while 1:
            msg = self.bus.poll(gst.MESSAGE_EOS | gst.MESSAGE_ERROR, gst.SECOND)
            if msg:
                break

        self.pipeline.set_state(gst.STATE_NULL)
        self.done()

    def run(self, buffers):
        self.buffers = buffers
        
        print '# Testing buffer processing rate for "fakesrc ! fakesink"'
        print '# bps = buffers per second'
        print '# spb = seconds per buffer'
        
        self.pipeline = self.build_pipeline(buffers)
        assert self.pipeline

        self.test()
    
def main(args):
    "GStreamer Buffers-Per-Second tester"

    if len(args) < 2:
        print 'usage: %s buffers' % args[0]
        return 1
    
    bps = BPS()
    
    buffers = int(args[1])
    if buffers < 1:
        print 'buffers must be higher than 0'
        return

    bps.run(buffers)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
