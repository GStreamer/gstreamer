#!/usr/bin/env python

import sys
import traceback
from math import pi

import pygtk
pygtk.require ("2.0")
import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst

import cairo

WIDTH, HEIGHT = 640, 480
FRAMES = 300
FRAMERATE = 15

class PyGstBufferDraw(gst.Element):
    _sinkpadtemplate = gst.PadTemplate ("sink",
                                         gst.PAD_SINK,
                                         gst.PAD_ALWAYS,
                                         gst.caps_from_string ("video/x-raw-rgb,bpp=32,depth=32,blue_mask=-16777216,green_mask=16711680, red_mask=65280, alpha_mask=255,width=[ 1, 2147483647 ],height=[ 1, 2147483647 ],framerate=[ 0/1, 2147483647/1 ]"))
    _srcpadtemplate = gst.PadTemplate ("src",
                                         gst.PAD_SRC,
                                         gst.PAD_ALWAYS,
                                         gst.caps_from_string ("video/x-raw-rgb,bpp=32,depth=32,blue_mask=-16777216,green_mask=16711680, red_mask=65280, alpha_mask=255,width=[ 1, 2147483647 ],height=[ 1, 2147483647 ],framerate=[ 0/1, 2147483647/1 ]"))

    def __init__(self):
        gst.Element.__init__(self)

        self.sinkpad = gst.Pad(self._sinkpadtemplate, "sink")
        self.sinkpad.set_chain_function(self.chainfunc)
        self.sinkpad.set_event_function(self.eventfunc)
        self.sinkpad.set_getcaps_function(gst.Pad.proxy_getcaps)
        self.sinkpad.set_setcaps_function(gst.Pad.proxy_setcaps)
        self.add_pad (self.sinkpad)

        self.srcpad = gst.Pad(self._srcpadtemplate, "src")

        self.srcpad.set_event_function(self.srceventfunc)
        self.srcpad.set_query_function(self.srcqueryfunc)
        self.srcpad.set_getcaps_function(gst.Pad.proxy_getcaps)
        self.srcpad.set_setcaps_function(gst.Pad.proxy_setcaps)
        self.add_pad (self.srcpad)

    def chainfunc(self, pad, buffer):
        try:
            outbuf = buffer.copy_on_write ()
            self.draw_on (outbuf)
            return self.srcpad.push (outbuf)
        except:
            return GST_FLOW_ERROR

    def eventfunc(self, pad, event):
        return self.srcpad.push_event (event)
        
    def srcqueryfunc (self, pad, query):
        return self.sinkpad.query (query)
    def srceventfunc (self, pad, event):
        return self.sinkpad.push_event (event)

    def draw_on (self, buf):
        try:
            caps = buf.get_caps()
            width = caps[0]['width']
            height = caps[0]['height']
            framerate = caps[0]['framerate']
            surface = cairo.ImageSurface.create_for_data (buf, cairo.FORMAT_ARGB32, width, height, 4 * width)
            ctx = cairo.Context(surface)
        except:
            print "Failed to create cairo surface for buffer"
            traceback.print_exc()
            return

        try:
            center_x = width/4
            center_y = 3*height/4

            # draw a circle
            radius = float (min (width, height)) * 0.25
            ctx.set_source_rgba (0.0, 0.0, 0.0, 0.9)
            ctx.move_to (center_x, center_y)
            ctx.arc (center_x, center_y, radius, 0, 2.0*pi)
            ctx.close_path()
            ctx.fill()
            ctx.set_source_rgba (1.0, 1.0, 1.0, 1.0)
            ctx.set_font_size(0.3 * radius)
            txt = "Hello World"
            extents = ctx.text_extents (txt)
            ctx.move_to(center_x - extents[2]/2, center_y + extents[3]/2)
            ctx.text_path(txt)
            ctx.fill()

        except:
            print "Failed cairo render"
            traceback.print_exc()

gobject.type_register(PyGstBufferDraw)

pipe = gst.Pipeline()
vt = gst.element_factory_make ("videotestsrc")
cf = gst.element_factory_make ("capsfilter")
c1 = PyGstBufferDraw()
color = gst.element_factory_make ("ffmpegcolorspace")
scale = gst.element_factory_make ("videoscale")
q1 = gst.element_factory_make ("queue")
sink = gst.element_factory_make ("autovideosink")

caps = gst.caps_from_string ("video/x-raw-rgb,width=%d,height=%d,framerate=%d/1" % (WIDTH, HEIGHT, FRAMERATE))
cf.set_property ("caps", caps)

vt.set_property ("num-buffers", FRAMES)

pipe.add (vt, cf, c1, q1, color, scale, sink)
gst.element_link_many (vt, cf, c1, q1, color, scale, sink)

def on_eos (bus, msg):
    mainloop.quit()

bus = pipe.get_bus()
bus.add_signal_watch()
bus.connect('message::eos', on_eos)

pipe.set_state (gst.STATE_PLAYING)

mainloop = gobject.MainLoop()
try:
    mainloop.run()
except:
    pass

pipe.set_state (gst.STATE_NULL)
pipe.get_state (gst.CLOCK_TIME_NONE)

