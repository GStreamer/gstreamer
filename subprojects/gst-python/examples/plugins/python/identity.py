#!/usr/bin/env python
# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4

# identity.py
# 2016 Marianna S. Buschle <msb@qtec.com>
#
# Simple identity element in python
#
# You can run the example from the source doing from gst-python/:
#
#  $ export GST_PLUGIN_PATH=$GST_PLUGIN_PATH:$PWD/plugin:$PWD/examples/plugins
#  $ GST_DEBUG=python:4 gst-launch-1.0 fakesrc num-buffers=10 ! identity_py ! fakesink

import gi
gi.require_version('GstBase', '1.0')

from gi.repository import Gst, GObject, GstBase
Gst.init_python()

#
# Simple Identity element created entirely in python
#
class Identity(GstBase.BaseTransform):
    __gstmetadata__ = ('Identity Python','Transform', \
                      'Simple identity element written in python', 'Marianna S. Buschle')

    __gsttemplates__ = (Gst.PadTemplate.new("src",
                                           Gst.PadDirection.SRC,
                                           Gst.PadPresence.ALWAYS,
                                           Gst.Caps.new_any()),
                       Gst.PadTemplate.new("sink",
                                           Gst.PadDirection.SINK,
                                           Gst.PadPresence.ALWAYS,
                                           Gst.Caps.new_any()))

    def do_transform_ip(self, buffer):
        Gst.info("timestamp(buffer):%s" % (Gst.TIME_ARGS(buffer.pts)))
        return Gst.FlowReturn.OK

GObject.type_register(Identity)
__gstelementfactory__ = ("identity_py", Gst.Rank.NONE, Identity)
