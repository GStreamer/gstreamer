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
from gstreamer import *
from gobject import GObject
import time
from identity import Identity

def update(sender, *args):
   print sender.get_name(), args

def build(filters, b):
   # create a new bin to hold the elements
   bin = gst_pipeline_new ('pipeline')

   src = gst_element_factory_make ('fakesrc', 'source');
   assert src
   src.set_property('silent', 1)
   src.set_property('num_buffers', b)

   sink = gst_element_factory_make ('fakesink', 'sink')
   assert sink
   sink.set_property('silent', 1)

   elements = [src] + filters + [sink]
   #  add objects to the main pipeline
   for e in elements: 
      bin.add(e)

   # connect the elements
   previous = None
   for e in elements:
      if previous:
         previous.connect(e)
      previous = e

   return bin

def filter(bin):
   bin.set_state(STATE_PLAYING);
   while bin.iterate(): pass
   bin.set_state(STATE_NULL)

ccnt = 0
def c():
   global ccnt
   id = gst_element_factory_make ('identity', 'c identity %d' % ccnt);
   assert id
   id.set_property('silent', 1)
   id.set_property('loop_based', 0)
   ccnt += 1
   return id

pcnt = 0
def py():
   id = Identity()
   assert id
   global pcnt
   id.set_name('py identity %d' % pcnt)
   pcnt += 1
   return id

def check(f, n, b):
   fs = []
   for i in range(n):
      fs.append(f())

   pipe = build(fs, b)

   start = time.time()
   ret = filter(pipe)
   print '%s b:%d i:%d t:%f' % (f, b, n, time.time() - start)
   return ret

def main():
   "Identity timer and latency check"
   gst_debug_set_categories(0L)

   if len(sys.argv) < 3:
      print 'usage: %s identites buffers' % (sys.argv[0],)
      return -1
   n = int(sys.argv[1])
   b = int(sys.argv[2])
   for f in (c, py):
      check(f, n, b)

if __name__ == '__main__':
   ret = main()
   sys.exit (ret)
