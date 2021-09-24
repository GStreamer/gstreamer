#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# gst-python
# Copyright (C) 2009 Stefan Kost   <ensonic@user.sf.net>
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

import sys

import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst


mainloop = gobject.MainLoop()

def on_eos(bus, msg):
   mainloop.quit()

def main(args):
   "Tagsetter test, test result with:"
   "gst-launch -t playbin uri=file://$PWD/test.avi"

   # create a new bin to hold the elements
   bin = gst.parse_launch('audiotestsrc num-buffers=100 ! ' +
                          'lame ! ' +
                          'avimux name=mux ! ' +
                          'filesink location=test.avi')

   mux = bin.get_by_name('mux')

   bus = bin.get_bus()
   bus.add_signal_watch()
   bus.connect('message::eos', on_eos)

   # prepare
   bin.set_state(gst.STATE_READY)
   
   # send tags
   l = gst.TagList()
   l[gst.TAG_ARTIST] = "Unknown Genius"
   l[gst.TAG_TITLE] = "Unnamed Artwork"
   mux.merge_tags(l, gst.TAG_MERGE_APPEND)

   # start playing
   bin.set_state(gst.STATE_PLAYING)

   try:
      mainloop.run()
   except KeyboardInterrupt:
      pass

   # stop the bin
   bin.set_state(gst.STATE_NULL)

if __name__ == '__main__':
   sys.exit(main(sys.argv))

