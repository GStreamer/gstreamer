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

import sys
import time
from gstreamer import *
from gobject import GObject
import gtk

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
            gtk.main_quit()

    def fakesrc(self, buffers):
        src = gst_element_factory_make('fakesrc','src')
        assert src
        src.set_property('silent', 1)
        src.set_property('num_buffers', buffers)
        return src

    def fakesink(self):
        sink = gst_element_factory_make('fakesink','sink')
        assert sink
        sink.set_property('silent', 1)
        return sink

    def build_pipeline(self, buffers):
        pipeline = Pipeline('pipeline')
        assert pipeline

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

        self.pipeline.set_state(STATE_PLAYING)

        if method == 'py':
            self.start = time.time()
            while self.pipeline.iterate():
                pass
        elif method == 'c':
            self.start = time.time()
            self.iter_id = add_iterate_bin(self.pipeline)
            gtk.main()
        elif method == 'gtk':
            self.start = time.time()
            gtk.idle_add(self.idle, self.pipeline)
            gtk.main()
        elif method == 'all':
            self.start = time.time()
            iterate_bin_all(self.pipeline)

        self.pipeline.set_state(STATE_NULL)

    def __main__(self):
        "GStreamer Buffers-Per-Second tester"
        gst_info_set_categories(0L)
        gst_debug_set_categories(0L)

        if len(sys.argv) < 2:
            print 'usage: %s buffers [method method ...]' % sys.argv[0]
            return 1
        else:
            self.buffers = int(sys.argv[1])
            self.methods = sys.argv[2:]
            if self.methods == []:
                self.methods = ('gtk', 'c', 'py', 'all')
 
        print '# Testing buffer processing rate for "fakesrc ! fakesink"'
        print '# gtk = gtk idle loop function in python'
        print '# c = gtk idle loop function in C'
        print '# py = full iterate loop in python'
        print '# all = full iterate loop in C'
        print '# bps = buffers per second'
        print '# spb = seconds per buffer'
        self.pipeline = self.build_pipeline(self.buffers)
        assert self.pipeline
        #self.pipeline.connect('deep-notify', self.notify)

        for m in self.methods:
            self.method = m
            self.test(m)

        return 0;

if __name__ == '__main__':
    bps = BPS()
    ret = bps.__main__()
    sys.exit (ret)
