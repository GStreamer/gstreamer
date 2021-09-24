#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# sinkelement.py
# (c) 2005 Edward Hervey <edward@fluendo.com>
# Licensed under LGPL
#
# Small test application to show how to write a sink element
# in 20 lines in python
#
# Run this script with GST_DEBUG=python:5 to see the debug
# messages

import pygst
pygst.require('0.10')
import gst
import gobject
gobject.threads_init ()

#
# Simple Sink element created entirely in python
#

class MySink(gst.Element):

    _sinkpadtemplate = gst.PadTemplate ("sinkpadtemplate",
                                        gst.PAD_SINK,
                                        gst.PAD_ALWAYS,
                                        gst.caps_new_any())

    def __init__(self):
        gst.Element.__init__(self)
        gst.info('creating sinkpad')
        self.sinkpad = gst.Pad(self._sinkpadtemplate, "sink")
        gst.info('adding sinkpad to self')
        self.add_pad(self.sinkpad)

        gst.info('setting chain/event functions')
        self.sinkpad.set_chain_function(self.chainfunc)
        self.sinkpad.set_event_function(self.eventfunc)
        
    def chainfunc(self, pad, buffer):
        self.info("%s timestamp(buffer):%d" % (pad, buffer.timestamp))
        return gst.FLOW_OK

    def eventfunc(self, pad, event):
        self.info("%s event:%r" % (pad, event.type))
        return True

gobject.type_register(MySink)

#
# Code to test the MySink class
#

src = gst.element_factory_make('fakesrc')
gst.info('About to create MySink')
sink = MySink()

pipeline = gst.Pipeline()
pipeline.add(src, sink)

src.link(sink)

pipeline.set_state(gst.STATE_PLAYING)

gobject.MainLoop().run()
