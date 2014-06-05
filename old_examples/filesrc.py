#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

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
# Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301, USA.

import sys
import gobject; gobject.threads_init()
import pygst
pygst.require('0.10')
import gst

class FileSource(gst.BaseSrc):
    __gsttemplates__ = (
        gst.PadTemplate("src",
                        gst.PAD_SRC,
                        gst.PAD_ALWAYS,
                        gst.caps_new_any()),
        )

    blocksize = 4096
    fd = None
    
    def __init__(self, name):
        self.__gobject_init__()
        self.curoffset = 0
        self.set_name(name)
            
    def set_property(self, name, value):
        if name == 'location':
            self.fd = open(value, 'r')

    def do_create(self, offset, size):
        if offset != self.curoffset:
            self.fd.seek(offset, 0)
        data = self.fd.read(self.blocksize)
        if data:
            self.curoffset += len(data)
            return gst.FLOW_OK, gst.Buffer(data)
        else:
            return gst.FLOW_UNEXPECTED, None
gobject.type_register(FileSource)

def main(args):
    if len(args) != 3:
        print 'Usage: %s input output' % (args[0])
        return -1
    
    bin = gst.Pipeline('pipeline')

    filesrc = FileSource('filesource')
    assert filesrc
    filesrc.set_property('location', args[1])
   
    filesink = gst.element_factory_make('filesink', 'sink')
    filesink.set_property('location', args[2])

    bin.add(filesrc, filesink)
    gst.element_link_many(filesrc, filesink)
    
    bin.set_state(gst.STATE_PLAYING);
    mainloop = gobject.MainLoop()

    def bus_event(bus, message):
        t = message.type
        if t == gst.MESSAGE_EOS:
            mainloop.quit()
        elif t == gst.MESSAGE_ERROR:
            err, debug = message.parse_error()
            print "Error: %s" % err, debug
            mainloop.quit()           
        return True
    bin.get_bus().add_watch(bus_event)

    mainloop.run()
    bin.set_state(gst.STATE_NULL)

if __name__ == '__main__':
   sys.exit(main(sys.argv))

