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
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.


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
        peaklevelpct = self.iec_scale(self.peaklevel)
        peakwidth = int(vumeter_width * (peaklevelpct/100))
        draw_gc = self.green_gc
        if self.peaklevel >= self.orange_threshold:
            draw_gc = self.orange_gc
        if self.peaklevel >= self.red_threshold:
            draw_gc = self.red_gc
        self.window.draw_rectangle(draw_gc, True,
                self.leftborder, self.topborder,
                peakwidth, vumeter_height)
 
        # draw yellow decay level
        if self.decaylevel > -90.0:
            decaylevelpct = self.iec_scale(self.decaylevel)
            decaywidth = int(vumeter_width * (decaylevelpct/100)) 
            self.window.draw_line(self.yellow_gc,
                self.leftborder + decaywidth,
                self.topborder,
                self.leftborder + decaywidth,
                self.topborder + vumeter_height)

        # draw tick marks
        # - 90.0 dB
        self.window.draw_line(self.style.black_gc, self.leftborder, 
            h - self.bottomborder, self.leftborder, 
            h - self.bottomborder + 5)
        layout = self.create_pango_layout("-90")
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder - int(layout_width/2),
            h - self.bottomborder + 7, layout)

        # -40.0 dB
        self.window.draw_line(self.style.black_gc, 
            self.leftborder + int(0.15*vumeter_width),
            h - self.bottomborder,
            self.leftborder + int(0.15*vumeter_width),
            h - self.bottomborder + 5)
        layout = self.create_pango_layout("-40")
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + int(0.15*vumeter_width) - int(layout_width/2),
            h - self.bottomborder + 7, layout)

        # -30.0 dB
        self.window.draw_line(self.style.black_gc, 
            self.leftborder + int(0.30*vumeter_width),
            h - self.bottomborder,
            self.leftborder + int(0.30*vumeter_width),
            h - self.bottomborder + 5)
        layout = self.create_pango_layout("-30")
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + int(0.30*vumeter_width) - int(layout_width/2),
            h - self.bottomborder + 7, layout)

        # -20.0 dB
        self.window.draw_line(self.style.black_gc, 
            self.leftborder + int(0.50*vumeter_width),
            h - self.bottomborder,
            self.leftborder + int(0.50*vumeter_width),
            h - self.bottomborder + 5)
        layout = self.create_pango_layout("-20")
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + int(0.50*vumeter_width) - int(layout_width/2),
            h - self.bottomborder + 7, layout)

        # -10.0dB
        self.window.draw_line(self.style.black_gc,
            self.leftborder + int(0.75*vumeter_width),
            h - self.bottomborder,
            self.leftborder + int(0.75*vumeter_width),
            h - self.bottomborder + 5)
        layout = self.create_pango_layout("-10")
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + int(0.75*vumeter_width) - int(layout_width/2),
            h - self.bottomborder + 7, layout)

        # - 5.0dB
        self.window.draw_line(self.style.black_gc,
            self.leftborder + int(0.875*vumeter_width),
            h - self.bottomborder,
            self.leftborder + int(0.875*vumeter_width),
            h - self.bottomborder + 5)
        layout = self.create_pango_layout("-5")
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + int(0.875*vumeter_width) - int(layout_width/2),
            h - self.bottomborder + 7, layout)

        # 0.0dB
        self.window.draw_line(self.style.black_gc,
            self.leftborder + vumeter_width,
            h - self.bottomborder,
            self.leftborder + vumeter_width,
            h - self.bottomborder + 5)
        layout = self.create_pango_layout("0")
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + vumeter_width - int(layout_width/2),
            h - self.bottomborder + 7, layout)

        # draw the value to the right
        layout = self.create_pango_layout("%.2fdB" % self.peaklevel)
        layout_width, layout_height = layout.get_pixel_size()
        self.window.draw_layout(self.style.black_gc,
            self.leftborder + vumeter_width + 5,
            self.topborder + int(vumeter_height/2 - layout_height/2),
            layout)

gobject.type_register(FVUMeter)
