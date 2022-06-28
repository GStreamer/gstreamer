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
# You can run the example from the source doing from gst-python/:
#
#  $ export GST_PLUGIN_PATH=$GST_PLUGIN_PATH:$PWD/plugin:$PWD/examples/plugins
#  $ GST_DEBUG=python:4 gst-launch-1.0 fakesrc num-buffers=10 ! mysink
#
# Since this implements the `mypysink://` protocol you can also do:
#
#  $ gst-launch-1.0 videotestsrc ! pymysink://

from gi.repository import Gst, GObject, GstBase
Gst.init_python()

#
# Simple Sink element created entirely in python
#
class MySink(GstBase.BaseSink, Gst.URIHandler):
    __gstmetadata__ = ('CustomSink','Sink', \
                      'Custom test sink element', 'Edward Hervey')

    __gsttemplates__ = Gst.PadTemplate.new("sink",
                                           Gst.PadDirection.SINK,
                                           Gst.PadPresence.ALWAYS,
                                           Gst.Caps.new_any())

    __protocols__ = ("pymysink",)
    __uritype__ = Gst.URIType.SINK

    def __init__(self):
        super().__init__()
        # Working around https://gitlab.gnome.org/GNOME/pygobject/-/merge_requests/129
        self.__dontkillme = self

    def do_render(self, buffer):
        Gst.info("timestamp(buffer):%s" % (Gst.TIME_ARGS(buffer.pts)))
        return Gst.FlowReturn.OK

    def do_get_uri(self, uri):
        return "pymysink://"

    def do_set_uri(self, uri):
        return True

GObject.type_register(MySink)
__gstelementfactory__ = ("mysink", Gst.Rank.NONE, MySink)
