#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>

# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
# 
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
# 
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

import sys

import gst

def handoff_cb(sender, *args):
   print sender.get_name(), args

def main(args):
   # create a new bin to hold the elements
   #gst_debug_set_categories(-1)
   bin = gst.parse_launch('fakesrc name=source silent=1 num-buffers=10 ! ' +
                          'fakesink name=sink silent=1')
   source = bin.get_by_name('source')
   source.connect('handoff', handoff_cb)
   sink = bin.get_by_name('source')
   sink.connect('handoff', handoff_cb)
   
   res = bin.set_state(gst.STATE_PLAYING);
   assert res

   while bin.iterate():
      pass

   res = bin.set_state(gst.STATE_NULL)
   assert res

if __name__ == '__main__':
   sys.exit(main(sys.argv))
