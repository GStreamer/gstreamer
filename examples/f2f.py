#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

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
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.
# 
# Author: David I. Lehn <dlehn@users.sourceforge.net>
#

import sys

import pygst
pygst.require('0.10')

import gst

def handoff_cb(sender, *args):
   print sender.get_name(), args

def main(args):
   # create a new bin to hold the elements
   #gst_debug_set_categories(-1)
   bin = gst.parse_launch('fakesrc name=source silent=1 num-buffers=10 signal-handoffs=true ! ' +
                          'fakesink name=sink silent=1 signal-handoffs=true')
   source = bin.get_by_name('source')
   source.connect('handoff', handoff_cb)
   source.get_pad("src").connect("have-data", handoff_cb)
   sink = bin.get_by_name('sink')
   sink.connect('handoff', handoff_cb)
   sink.get_pad("sink").connect('have-data', handoff_cb)

   print source, sink

   bus = bin.get_bus()
   
   res = bin.set_state(gst.STATE_PLAYING);
   assert res

   while 1:
      msg = bus.poll(gst.MESSAGE_EOS | gst.MESSAGE_ERROR, gst.SECOND)
      if msg:
         break

   res = bin.set_state(gst.STATE_NULL)
   assert res

if __name__ == '__main__':
   sys.exit(main(sys.argv))
