#!/usr/bin/env python
#
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
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>
#

import pygtk
pygtk.require('2.0')

import sys
import time
import gobject
import gtk
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

    def eos(self, sink):
        self.done()
        if self.method in ('gtk', 'c'):
            gst.main_quit()

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
        sink.connect('eos', self.eos)
        src.link(sink)

        return pipeline

    def notify(self, sender, obj, arg):
        prop = obj.get_property(arg.name)
        print 'notify', sender, arg.name, prop
        print prop

    def idle(self, pipeline):
        return pipeline.iterate()

    def test(self, method):
        print '%s:' % (method,),
        self.method = method
        
	print self.pipeline.get_state()
        self.pipeline.set_state(gst.STATE_PLAYING)
	print self.pipeline.get_state()

        if method == 'py':
            self.start = time.time()
            while self.pipeline.iterate():
                pass
        elif method == 'c':
            self.start = time.time()
            gobject.idle_add(self.pipeline.iterate)
            gst.main()
        #elif method == 'gst':
        #    self.start = time.time()
        #    gtk.idle_add(self.idle, self.pipeline)
        #    gtk.main()

        self.pipeline.set_state(gst.STATE_NULL)

    def run(self, buffers, methods):
        self.buffers = buffers
        
        print '# Testing buffer processing rate for "fakesrc ! fakesink"'
        #print '# gst = gtk idle loop function in python'
        print '# c = gtk idle loop function in C'
        print '# py = full iterate loop in python'
        print '# all = full iterate loop in C'
        print '# bps = buffers per second'
        print '# spb = seconds per buffer'
        
        self.pipeline = self.build_pipeline(buffers)
        assert self.pipeline
        #self.pipeline.connect('deep-notify', self.notify)
        
        map(self.test, methods)
    
def main(args):
    "GStreamer Buffers-Per-Second tester"

    if len(args) < 2:
        print 'usage: %s buffers [method method ...]' % args[0]
        return 1
    
    bps = BPS()
    
    buffers = int(args[1])
    if buffers < 0:
	print 'buffers must be higher than 0'
	return

    methods = args[2:]
    if not methods:
        methods = ('gtk', 'c', 'py', 'all')

    bps.run(buffers, methods)

if __name__ == '__main__':
    sys.exit(main(sys.argv))
