#!/usr/bin/env python

import pygtk
pygtk.require ("2.0")
import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst

class PyIdentity(gst.Element):
    _sinkpadtemplate = gst.PadTemplate ("sink",
                                         gst.PAD_SINK,
                                         gst.PAD_ALWAYS,
                                         gst.caps_new_any())
    _srcpadtemplate = gst.PadTemplate ("src",
                                         gst.PAD_SRC,
                                         gst.PAD_ALWAYS,
                                         gst.caps_new_any())

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
        gst.log ("Passing buffer with ts %d" % (buffer.timestamp))
        return self.srcpad.push (buffer)

    def eventfunc(self, pad, event):
        return self.srcpad.push_event (event)
        
    def srcqueryfunc (self, pad, query):
        return self.sinkpad.query (query)
    def srceventfunc (self, pad, event):
        return self.sinkpad.push_event (event)

gobject.type_register(PyIdentity)

pipe = gst.Pipeline()
vt = gst.element_factory_make ("videotestsrc")
i1 = PyIdentity()
color = gst.element_factory_make ("ffmpegcolorspace")
scale = gst.element_factory_make ("videoscale")
q1 = gst.element_factory_make ("queue")
i2 = PyIdentity()
sink = gst.element_factory_make ("autovideosink")

pipe.add (vt, i1, q1, i2, color, scale, sink)
gst.element_link_many (vt, i1, q1, i2, color, scale, sink)

pipe.set_state (gst.STATE_PLAYING)

gobject.MainLoop().run()
