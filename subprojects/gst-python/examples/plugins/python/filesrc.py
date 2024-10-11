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
import gi
import sys

gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
from gi.repository import GLib, Gst, GObject, GstBase


if __name__ == '__main__':
    Gst.init(None)
Gst.init_python()


class FileSource(GstBase.BaseSrc):
    __gstmetadata__ = ('PyFileSource', 'Source',
                       'Custom Python filesrc example', 'Jan Schmidt')

    __gsttemplates__ = (
        Gst.PadTemplate.new("src",
                            Gst.PadDirection.SRC,
                            Gst.PadPresence.ALWAYS,
                            Gst.Caps.new_any()),
    )

    blocksize = 4096
    fd = None

    def __init__(self, name):
        super().__init__()
        self.curoffset = 0
        self.set_name(name)

    def set_property(self, name, value):
        if name == 'location':
            self.fd = open(value, 'rb')

    def do_create(self, offset, size, wot):
        if offset != self.curoffset:
            self.fd.seek(offset, 0)
        data = self.fd.read(self.blocksize)
        if data:
            self.curoffset += len(data)
            return Gst.FlowReturn.OK, Gst.Buffer.new_wrapped(data)
        else:
            return Gst.FlowReturn.EOS, None


GObject.type_register(FileSource)
__gstelementfactory__ = ("filesrc_py", Gst.Rank.NONE, FileSource)


def main(args):
    Gst.init()

    if len(args) != 3:
        print(f'Usage: {args[0]} input output')
        return -1

    bin = Gst.Pipeline('pipeline')

    filesrc = FileSource('filesource')
    assert filesrc
    filesrc.set_property('location', args[1])

    filesink = Gst.ElementFactory.make('filesink', 'sink')
    filesink.set_property('location', args[2])

    bin.add(filesrc, filesink)
    Gst.Element.link_many(filesrc, filesink)

    bin.set_state(Gst.State.PLAYING)
    mainloop = GLib.MainLoop()

    def bus_event(bus, message):
        t = message.type
        if t == Gst.MessageType.EOS:
            mainloop.quit()
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"Error: {err}, {debug}")
            mainloop.quit()
        return True
    bus = bin.get_bus()
    bus.add_signal_watch()
    bus.connect('message', bus_event)

    mainloop.run()
    bin.set_state(Gst.State.NULL)


if __name__ == '__main__':
    sys.exit(main(sys.argv))
