#!/usr/bin/env python
#
# gst-python
# Copyright (C) 2002 David I. Lehn
#               2004 Johan Dahlin
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
import gobject
import gst

class Identity(gst.Element):
   def __init__(self):
      self.__gobject_init__()
      self.sinkpad = gst.Pad('sink', gst.PAD_SINK)
      self.add_pad(self.sinkpad)
      self.sinkpad.set_chain_function(self.chain)
      self.sinkpad.set_link_function(self.pad_link)

      self.srcpad = gst.Pad('src', gst.PAD_SRC)
      self.add_pad(self.srcpad)
      self.srcpad.set_link_function(self.pad_link)

   def get_bufferpool(self, pad):
      print 'get_bufferpool:', self, pad
      return self.srcpad.get_bufferpool()

   def pad_link(self, pad, caps):
      print 'pad_link:', self, pad, caps
      return gst.PAD_LINK_OK

   def chain(self, pad, buf):
      self.srcpad.push(buf)
      
gobject.type_register(Identity)

def filter(element):
   # create a new bin to hold the elements
   bin = gst.Pipeline('pipeline')

   filesrc = gst.Element('sinesrc', 'source');
   filesink = gst.Element('fakesink', 'sink')

   stats = gst.Element('statistics', 'stats');
   stats.set_property('silent', False)
   stats.set_property('buffer_update_freq', True)
   stats.set_property('update_on_eos', True)

   bin.add_many(filesrc, element, stats, filesink)
   gst.element_link_many(filesrc, element, stats, filesink)

   # start playing
   bin.set_state(gst.STATE_PLAYING);

   while bin.iterate():
      pass

   # stop the bin
   bin.set_state(gst.STATE_NULL)

def main(args):
   "A GStreamer Python subclassing example of an identity filter"
   print "This example is not finished."
   sys.exit(1)

   identity = Identity()
   identity.set_name('identity')
   if not identity:
      print 'could not create \"Identity\" element'
      return -1

   return filter(identity)

if __name__ == '__main__':
   sys.exit(main(sys.argv))

