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

def main():
   "Basic example to play an Ogg Vorbis stream through OSS"
   #gst_debug_set_categories(-1)

   if len(sys.argv) != 2:
      print 'usage: %s <Ogg Vorbis file>' % (sys.argv[0])
      return -1

   # create a new bin to hold the elements
   bin = gst_pipeline_new ('pipeline')

   # create a disk reader
   filesrc = gst_element_factory_make ('filesrc', 'disk_source');
   if not filesrc:
      print 'could not find plugin \"filesrc\"'
      return -1
   filesrc.set_property('location', sys.argv[1])

   # now get the decoder
   decoder = gst_element_factory_make ('vorbisdec', 'parse');
   if not decoder:
      print 'could not find plugin \"vorbisdec\"'
      return -1

   # and an audio sink
   osssink = gst_element_factory_make ('osssink', 'play_audio')
   if not osssink:
      print 'could not find plugin \"osssink\"'
      return -1

   #  add objects to the main pipeline
   for e in (filesrc, decoder, osssink):
      bin.add(e)

   # connect the elements
   previous = None
   for e in (filesrc, decoder, osssink):
      if previous:
         previous.connect(e)
      previous = e

   # start playing
   bin.set_state(STATE_PLAYING);

   while bin.iterate(): pass

   # stop the bin
   bin.set_state(STATE_NULL)

   return 0

if __name__ == '__main__':
   ret = main()
   sys.exit(ret)
