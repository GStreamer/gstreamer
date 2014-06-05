#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# sinkelement.py
# (c) 2005 Edward Hervey <edward@fluendo.com>
# (c) 2007 Jan Schmidt <jan@fluendo.com>
# Licensed under LGPL
#
# Small test application to show how to write a sink element
# in 20 lines in python and place into the gstreamer registry
# so it can be autoplugged or used from parse_launch.
#
# Run this script with GST_DEBUG=python:5 to see the debug
# messages

from gi.repository import Gst, GObject

#
# Simple Sink element created entirely in python
#
class MySink(Gst.Element):
    __gstmetadata__ = ('CustomSink','Sink', \
                      'Custom test sink element', 'Edward Hervey')

    __gsttemplates__ = Gst.PadTemplate.new ("sinkpadtemplate",
                                            Gst.PadDirection.SINK,
                                            Gst.PadPresence.ALWAYS,
                                            Gst.Caps.new_any())

    def __init__(self):
        Gst.Element.__init__(self)
        Gst.info('creating sinkpad')
        self.sinkpad = Gst.Pad.new_from_template(self.__gsttemplates__, "sink")
        Gst.info('adding sinkpad to self')
        self.add_pad(self.sinkpad)

        Gst.info('setting chain/event functions')
        self.sinkpad.set_chain_function(self.chainfunc)
        self.sinkpad.set_event_function(self.eventfunc)
        st = Gst.Structure.from_string("yes,fps=1/2")[0]

    def chainfunc(self, pad, buffer):
        Gst.info("%s timestamp(buffer):%d" % (pad, buffer.pts))
        return Gst.FlowReturn.OK

    def eventfunc(self, pad, event):
        Gst.info("%s event:%r" % (pad, event.type))
        return True

GObject.type_register(MySink)
__gstelementfactory__ = ("mysink", Gst.Rank.NONE, MySink)
