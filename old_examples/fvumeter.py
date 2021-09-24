# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# gst-python
# Copyright (C) 2005 Fluendo S.L.
# Originally from the Flumotion streaming server.
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
# Author: Zaheer Merali <zaheermerali at gmail dot com>

import gtk
from gtk import gdk
import gobject


# this VUMeter respects IEC standard
# BS 6840-18:1996/IEC-268-18
# and is inspired by JACK's meterbridge dpm_meters.c

class FVUMeter(gtk.DrawingArea):
    __gsignals__ = { 'expose-event' : 'override',
                     'size-allocate': 'override',
                     'size-request': 'override',
                     'realize' : 'override'
             }
    __gproperties__ = {
        'peak' : (gobject.TYPE_FLOAT,
                  'peak volume level',
                  'peak volume level in dB',
                  -90.0,
                  0,
                  -90.0,
                  gobject.PARAM_READWRITE),
        'decay' : (gobject.TYPE_FLOAT,
                   'decay volume level',
                   'decay volume level in dB',
                   -90.0,
                   0,
                   -90.0,
                   gobject.PARAM_READWRITE),
        'orange-threshold': (gobject.TYPE_FLOAT,
                            'threshold for orange',
                            'threshold for orange use in dB',
                            -90.0,
                            0,
                            -10.0,
                            gobject.PARAM_READWRITE),
        'red-threshold': (gobject.TYPE_FLOAT,
                         'threshold for red',
                         'threshold for red use in dB',
                         -90.0,
                         0,
                         -1.0,
                         gobject.PARAM_READWRITE)
                            
    }
    green_gc = None
    orange_gc = None
    red_gc = None
    yellow_gc = None
    
    topborder = 7
    peaklevel = -90.0
    decaylevel = -90.0
    orange_threshold = -10.0
    red_threshold = -1.0
    bottomborder = 25
    leftborder = 15 
    rightborder = 65 

    # Returns the meter deflection percentage given a db value
    def iec_scale(self, db):
        pct = 0.0

        if db < -70.0:
            pct = 0.0
        elif db < -60.0:
            pct = (db + 70.0) * 0.25
        elif db < -50.0:
            pct = (db + 60.0) * 0.5 + 2.5
        elif db < -40.0:
            pct = (db + 50.0) * 0.75 + 7.5
        elif db < -30.0:
            pct = (db + 40.0) * 1.5 + 15.0
        elif db < -20.0:
            pct = (db + 30.0) * 2.0 + 30.0
        elif db < 0.0:
            pct = (db + 20.0) * 2.5 + 50.0
        else:
            pct = 100.0

        return pct

    def do_get_property(self, property):
        if property.name == 'peak':
            return self.peaklevel
        elif property.name == 'decay':
            return self.decaylevel
        elif property.name == 'orange-threshold':
            return self.orange_threshold
        elif property.name == 'red-threshold':
            return self.red_threshold
        else:
            raise AttributeError, 'unknown property %s' % property.name

    def do_set_property(self, property, value):
        if property.name == 'peak':
            self.peaklevel = value
        elif property.name == 'decay':
            self.decaylevel = value
        elif property.name == 'orange-threshold':
            self.orange_threshold = value
        elif property.name == 'red-threshold':
            self.red_threshold = value
        else:
            raise AttributeError, 'unknown property %s' % property.name

        self.queue_draw()
                
    def do_size_request(self, requisition):
        requisition.width = 250 
        requisition.height = 50

    def do_size_allocate(self, allocation):
        self.allocation = allocation
        if self.flags() & gtk.REALIZED:
            self.window.move_resize(*allocation)
    
    def do_realize(self):
        self.set_flags(self.flags() | gtk.REALIZED)

        self.window = gdk.Window(self.get_parent_window(),
                                 width=self.allocation.width,
                                 height=self.allocation.height,
                                 window_type=gdk.WINDOW_CHILD,
                                 wclass=gdk.INPUT_OUTPUT,
                                 event_mask=self.get_events() | gdk.EXPOSURE_MASK)

        colormap = gtk.gdk.colormap_get_system()
        green = colormap.alloc_color(0, 65535, 0)
        orange = colormap.alloc_color(65535, 32768, 0)
        red = colormap.alloc_color(65535, 0, 0)
        yellow = colormap.alloc_color(65535, 65535, 0)
        self.green_gc = gdk.GC(self.window, foreground=green)
        self.orange_gc = gdk.GC(self.window, foreground=orange)
        self.red_gc = gdk.GC(self.window, foreground=red)
        self.yellow_gc = gdk.GC(self.window, foreground=yellow)
 
        self.window.set_user_data(self)
        self.style.attach(self.window)
        self.style.set_background(self.window, gtk.STATE_NORMAL)

    def do_expose_event(self, event):
        self.chain(event)
       
        x, y, w, h = self.allocation
        vumeter_width = w - (self.leftborder + self.rightborder)
        vumeter_height = h - (self.topborder + self.bottomborder)
        self.window.draw_rectangle(self.style.black_gc, True,
                                   self.leftborder, self.topborder,
                                   vumeter_width, 
                                   vumeter_height)
        # draw peak level
        # 0 maps to width of 0, full scale maps to total width
        peaklevelpct = self.iec_scale(self.peaklevel)
        peakwidth = int(vumeter_width * (peaklevelpct / 100))
        draw_gc = self.green_gc
        if self.peaklevel >= self.orange_threshold:
            draw_gc = self.orange_gc
        if self.peaklevel >= self.red_threshold:
            draw_gc = self.red_gc
        if peakwidth > 0:
            self.window.draw_rectangle(draw_gc, True,
                    self.leftborder, self.topborder,
                    peakwidth, vumeter_height)
     
        # draw yellow decay level
        if self.decaylevel > -90.0:
            decaylevelpct = self.iec_scale(self.decaylevel)
            decaywidth = int(vumeter_width * (decaylevelpct / 100))
            # cheat the geometry by drawing 0% level at pixel 0,
            # which is same position as just above 0%
            if decaywidth == 0:
                decaywidth = 1
            self.window.draw_line(self.yellow_gc,
                self.leftborder + decaywidth - 1,
                self.topborder,
                self.leftborder + decaywidth - 1,
                self.topborder + vumeter_height - 1)

        # draw tick marks
        scalers = [
            ('-90', 0.0),
            ('-40', 0.15),
            ('-30', 0.30),
            ('-20', 0.50),
            ('-10', 0.75),
            ( '-5', 0.875),
            (  '0', 1.0),
        ]
        for level, scale in scalers:
            # tick mark, 6 pixels high
            # we cheat again here by putting the 0 at the first pixel
            self.window.draw_line(self.style.black_gc, 
                self.leftborder + int(scale * (vumeter_width - 1)),
                h - self.bottomborder,
                self.leftborder + int(scale * (vumeter_width - 1)),
                h - self.bottomborder + 5)
            # tick label
            layout = self.create_pango_layout(level)
            layout_width, layout_height = layout.get_pixel_size()
            self.window.draw_layout(self.style.black_gc,
                self.leftborder + int(scale * vumeter_width)
                    - int(layout_width / 2),
                h - self.bottomborder + 7, layout)

        # draw the peak level to the right
        layout = self.create_pango_layout("%.2fdB" % self.peaklevel)
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + vumeter_width + 5,
            self.topborder + int(vumeter_height / 2 - layout_height / 2),
            layout)

gobject.type_register(FVUMeter)
