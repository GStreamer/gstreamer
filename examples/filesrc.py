#!/usr/bin/env python
#
# GStreamer python bindings
# Copyright (C) 2002 David I. Lehn <dlehn@users.sourceforge.net>
#               2004 Johan Dahlin  <johan@gnome.org>
#
# filesrc.py: implements a file source element completely in python
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

import sys
import gobject
import gst

class FileSource(gst.Element):
    blocksize = 4096
    fd = None
    def __init__(self, name):
        self.__gobject_init__()
        self.set_name(name)
        self.srcpad = gst.Pad('src', gst.PAD_SRC)
        self.srcpad.set_get_function(self.srcpad_get)
        self.add_pad(self.srcpad)
            
    def set_property(self, name, value):
        if name == 'location':
            self.fd = open(value, 'r')
            
    def srcpad_get(self, pad):
        data = self.fd.read(self.blocksize)
        if data:
            return gst.Buffer(data)
        else:
            self.set_eos()
            return gst.Event(gst.EVENT_EOS)
gobject.type_register(FileSource)

def main(args):
    print 'This example is not finished yet.'
    return

    if len(args) != 3:
        print 'Usage: %s input output' % (args[0])
        return -1
    
    bin = gst.Pipeline('pipeline')

    filesrc = FileSource('filesource')
    #filesrc = gst.Element('filesrc', 'src')
    assert filesrc
    filesrc.set_property('location', args[1])
   
    filesink = gst.element_factory_make('filesink', 'sink')
    filesink.set_property('location', args[2])

    bin.add_many(filesrc, filesink)
    gst.element_link_many(filesrc, filesink)
    
    bin.set_state(gst.STATE_PLAYING);

    while bin.iterate():
        pass

    bin.set_state(gst.STATE_NULL)

if __name__ == '__main__':
   sys.exit(main(sys.argv))

