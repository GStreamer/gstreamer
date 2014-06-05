#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# gst-python
# Copyright (C) 2005 Fluendo S.L.
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
# Author: Andy Wingo <wingo@pobox.com>

import gtk
from gtk import gdk
import gobject

import pygst
pygst.require('0.10')
import gst

class DebugSlider(gtk.HScale):
    def __init__(self):
        adj = gtk.Adjustment(int(gst.debug_get_default_threshold()),
                             0, 5, 1, 0, 0)
        gtk.HScale.__init__(self, adj)
        self.set_digits(0)
        self.set_draw_value(True)
        self.set_value_pos(gtk.POS_TOP)

        def value_changed(self):
            newlevel = int(self.get_adjustment().get_value())
            gst.debug_set_default_threshold(newlevel)

        self.connect('value-changed', value_changed)

if __name__ == '__main__':
    p = gst.parse_launch('fakesrc ! fakesink')
    p.set_state(gst.STATE_PLAYING)

    w = gtk.Window()
    s = DebugSlider()
    w.add(s)
    s.show()
    w.set_default_size(200, 40)
    w.show()
    w.connect('delete-event', lambda *args: gtk.main_quit())
    gtk.main()
