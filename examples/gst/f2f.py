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

from gobject import GObject
from gstreamer import *

def handoff(sender, *args):
   print sender.get_name(), args

def main():
   # create a new bin to hold the elements
   #gst_debug_set_categories(-1)
   bin = gst_pipeline_new ('pipeline')
   assert bin

   src = gst_element_factory_make ('fakesrc', 'src')
   assert src
   GObject.connect(src, 'handoff', handoff)
   src.set_property('silent', 1)
   src.set_property('num_buffers', 10)

   sink = gst_element_factory_make ('fakesink', 'sink')
   assert sink
   GObject.connect(sink, 'handoff', handoff)
   src.set_property('silent', 1)

   #  add objects to the main pipeline
   for e in (src, sink):
      bin.add(e)

   # connect the elements
   res = src.connect(sink)
   assert res

   # start playing
   res = bin.set_state(STATE_PLAYING);
   assert res

   while bin.iterate(): pass

   # stop the bin
   res = bin.set_state(STATE_NULL)
   assert res

if __name__ == '__main__':
   main()
