#!/usr/bin/env python
#
# gst-python
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>
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

import gst

def filter(input, output):
   "A GStreamer copy pipeline which can add arbitrary filters"

   # create a new bin to hold the elements
   bin = gst.Pipeline('pipeline')

   filesrc = gst.Element('filesrc', 'source');
   filesrc.set_property('location', input)

   stats = gst.Element('statistics', 'stats');
   stats.set_property('silent', False)
   stats.set_property('buffer_update_freq', True)
   stats.set_property('update_on_eos', True)
   
   filesink = gst.Element('filesink', 'sink')
   filesink.set_property('location', output)

   bin.add_many(filesrc, stats, filesink)
   gst.element_link_many(filesrc, stats, filesink)

   # start playing
   bin.set_state(gst.STATE_PLAYING);

   while bin.iterate():
      pass

   # stop the bin
   bin.set_state(gst.STATE_NULL)

def main(args):
   "A GStreamer based cp(1) with stats"

   if len(args) != 3:
      print 'usage: %s source dest' % (sys.argv[0])
      return -1

   return filter(args[1], args[2])

if __name__ == '__main__':
   sys.exit(main(sys.argv))
