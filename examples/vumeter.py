#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# gst-python
# Copyright (C) 2005 Andy Wingo <wingo@pobox.com>
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


# A test more of gst-plugins than of gst-python.


import pygtk
pygtk.require('2.0')
import gtk
import gobject

import pygst
pygst.require('0.10')
import gst

import fvumeter


def clamp(x, min, max):
    if x < min:
        return min
    elif x > max:
        return max
    return x


class Window(gtk.Dialog):
    def __init__(self):
        gtk.Dialog.__init__(self, 'Volume Level')
        self.prepare_ui()

    def prepare_ui(self):
        self.set_default_size(200,60)
        self.set_title('Volume Level')
        self.connect('delete-event', lambda *x: gtk.main_quit())
        self.vus = []
        self.vus.append(fvumeter.FVUMeter())
        self.vus.append(fvumeter.FVUMeter())
        self.vbox.add(self.vus[0])
        self.vbox.add(self.vus[1])
        self.vus[0].show()
        self.vus[1].show()

    def error(self, message, secondary=None):
        m = gtk.MessageDialog(self,
                              gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
                              gtk.MESSAGE_ERROR,
                              gtk.BUTTONS_OK,
                              message)
        if secondary:
            m.format_secondary_text(secondary)
        m.run()
                              
    def on_message(self, bus, message):
        if  message.structure.get_name() == 'level':
            s = message.structure
            for i in range(0, len(s['peak'])):
                self.vus[i].freeze_notify()
                decay = clamp(s['decay'][i], -90.0, 0.0)
                peak = clamp(s['peak'][i], -90.0, 0.0)
                if peak > decay:
                    print "ERROR: peak bigger than decay!"
            
                self.vus[i].set_property('decay', decay)
                self.vus[i].set_property('peak', peak)
        return True

    def run(self):
        try:
            self.set_sensitive(False)
            s = 'alsasrc ! level message=true ! fakesink'
            pipeline = gst.parse_launch(s)
            self.set_sensitive(True)
            pipeline.get_bus().add_signal_watch()
            i = pipeline.get_bus().connect('message::element', self.on_message)
            pipeline.set_state(gst.STATE_PLAYING)
            gtk.Dialog.run(self)
            pipeline.get_bus().disconnect(i)
            pipeline.get_bus().remove_signal_watch()
            pipeline.set_state(gst.STATE_NULL)
        except gobject.GError, e:
            self.set_sensitive(True)
            self.error('Could not create pipeline', e.__str__)
        
if __name__ == '__main__':
    w = Window()
    w.show_all()
    w.run()
