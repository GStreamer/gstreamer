#!/usr/bin/env python2.2
#
# gst-python
# Copyright (C) 2002 David I. Lehn
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
# Author: David I. Lehn <dlehn@vt.edu>
#

import sys
import time
from gstreamer import *
from gobject import GObject

def update(sender, *args):
   print sender.get_name(), args

max = 0
min = -1
total = 0
count = 0
print_del = 1
interations = 0

def handoff_src(src, buf):
   #buf.set_timestamp(time.time())
   pass

def handoff_sink(sink, buf):
   global max, min, total, count

   end = time.time()
   #d = end - buf.get_timestamp()
   d = end - 0
   if d > max:
      max = d
   if d < min:
      min = d
   total += d
   count += 1
   avg = total/count

   if (count % print_del) == 0:
      print '%07d:%08d min:%08d max:%08d avg:%f\n' %\
            (count, d, min, max, avg),

def identity_add(pipeline, first, count):
   last = first

   for i in range(count):
     name = 'identity_%03d' % i
     ident = gst_elementfactory_make('identity', name)
     assert ident
     ident.set_property('silent', 1)
     pipeline.add(ident)
     last.get_pad('src').connect(ident.get_pad('sink'))
     last = ident

   return last

def fakesrc():
   src = gst_elementfactory_make('fakesrc','src')
   assert src
   src.set_property('silent', 1)
   src.set_property('num_buffers', iterations)
   GObject.connect(src, 'handoff', handoff_src)
   return src

def fakesink():
   sink = gst_elementfactory_make('fakesink','fakesink')
   assert sink
   sink.set_property('silent', 1)
   GObject.connect(sink, 'handoff', handoff_sink)
   return sink

def simple(argv):
   if len(argv) < 1:
     print 'simple: bad params'
     return None
   idents = int(argv[0])
   if len(argv) == 2:
      gst_schedulerfactory_set_default_name (argv[1])

   pipeline = gst_pipeline_new('pipeline')
   assert pipeline

   src = fakesrc()
   pipeline.add(src)
   last = identity_add(pipeline, src, idents)
   sink = fakesink()
   pipeline.add(sink)
   last.get_pad('src').connect(sink.get_pad('sink'))

   return pipeline

def queue(argv):
   if len(argv) < 1:
      print 'queue: bad params'
      return None
   idents = int(argv[0])

   if len(arv) == 2:
      gst_schedulerfactory_set_default_name (argv[1])

   pipeline = gst_pipeline_new('pipeline')
   assert pipeline

   src_thr = gst_thread_new('src_thread')
   assert src_thr

   src = fakesrc()
   assert src
   src_thr.add(src)

   src_q = gst_elementfactory_make('queue','src_q')
   assert src_q
   src_thr.add(src_q)
   src.get_pad('src').connect(src_q.get_pad('sink'))

   pipeline.add(src_thr)

   last = identity_add(pipeline, src_q, idents)

   sink_q = gst_elementfactory_make('queue','sink_q')
   assert sink_q
   pipeline.add(sink_q)
   last.get_pad('src').connect(sink_q.get_pad('sink'))

   sink_thr = gst_thread_new('sink_thread')
   assert sink_thr

   sink = fakesink()
   assert sink

   sink_thr.add(sink)

   pipeline.add(sink_thr)

   sink_q.get_pad('src').connect(sink.get_pad('sink'))

   return pipeline

tests = {
   'simple' : ('ident_count [scheduler_name]', simple),
   'queue' : ('ident_count [scheduler_name]', queue),
}

def main():
   "A GStreamer latency tester"
   #gst_debug_set_categories(-1)
   global iterations, print_del

   if len(sys.argv) < 3:
      print 'usage: %s iterations print_del test_name [test_params...]' % sys.argv[0]
      for name in tests.keys():
         doc, func = tests[name]
         print '  %s %s' % (name, doc)
      return -1
   else:
      iterations = int(sys.argv[1])
      print_del = int(sys.argv[2])
      name = sys.argv[3]
 
   pipeline = tests[name][1](sys.argv[4:])
   assert pipeline

   #xmlSaveFile('lat.gst', gst_xml_write(pipeline))

   pipeline.set_state(STATE_PLAYING)

   while count < iterations:
      pipeline.iterate()

   pipeline.set_state(STATE_NULL)

   print

   return 0;

if __name__ == '__main__':
   ret = main()
   sys.exit (ret)
