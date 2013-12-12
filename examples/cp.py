#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

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
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>
#

import sys

import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst


mainloop = gobject.MainLoop()

def on_eos(bus, msg):
   mainloop.quit()

def filter(input, output):
   "A GStreamer copy pipeline which can add arbitrary filters"

   # create a new bin to hold the elements
   bin = gst.parse_launch('filesrc name=source ! ' +
                          'progressreport ! ' +
                           # This 'statistics' element is depreciated in 0.10
                          #'statistics silent=false buffer-update-freq=1 ' +
                          #'update_on_eos=true ! ' +
                          'filesink name=sink')
   filesrc = bin.get_by_name('source')
   filesrc.set_property('location', input)

   filesink = bin.get_by_name('sink')
   filesink.set_property('location', output)

   bus = bin.get_bus()
   bus.add_signal_watch()
   bus.connect('message::eos', on_eos)

   # start playing
   bin.set_state(gst.STATE_PLAYING)

   try:
      mainloop.run()
   except KeyboardInterrupt:
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
