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
import gobject
from identity import Identity
from cp import filter

class Rot13(Identity):
   def chain(self, pad, buf):
      # override Identity's chain
      data = gst_buffer_get_data(buf)
      data2 = ''
      # waste cycles
      for c in data:
         if c.isalpha():
            if c.islower():
               a = 'a'
            else:
               a = 'A'
            c = chr((((ord(c) - ord(a)) + 13) % 26) + ord(a))
         data2 = data2 + c
      newbuf = gst_buffer_new()
      gst_buffer_set_data(newbuf, data2)
      self.srcpad.push(newbuf)

gobject.type_register(Rot13)

def main():
   "A GStreamer Python subclassing example of a rot13 filter"
   gst_debug_set_categories(0)

   rot13 = Rot13()
   rot13.set_name('rot13')
   if not rot13:
      print 'could not create \"Rot13\" element'
      return -1

   return filter([rot13])

if __name__ == '__main__':
   ret = main()
   sys.exit (ret)
