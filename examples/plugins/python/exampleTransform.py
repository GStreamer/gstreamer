#!/usr/bin/python3
# exampleTransform.py
# 2019 Daniel Klamt <graphics@pengutronix.de>

# Inverts a grayscale image in place, requires numpy.
#
# gst-launch-1.0 videotestsrc ! ExampleTransform ! videoconvert ! xvimagesink

import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GstVideo', '1.0')

from gi.repository import Gst, GObject, GstBase, GstVideo

import numpy as np

Gst.init(None)
FIXED_CAPS = Gst.Caps.from_string('video/x-raw,format=GRAY8,width=[1,2147483647],height=[1,2147483647]')

class ExampleTransform(GstBase.BaseTransform):
    __gstmetadata__ = ('ExampleTransform Python','Transform',
                      'example gst-python element that can modify the buffer gst-launch-1.0 videotestsrc ! ExampleTransform ! videoconvert ! xvimagesink', 'dkl')

    __gsttemplates__ = (Gst.PadTemplate.new("src",
                                           Gst.PadDirection.SRC,
                                           Gst.PadPresence.ALWAYS,
                                           FIXED_CAPS),
                       Gst.PadTemplate.new("sink",
                                           Gst.PadDirection.SINK,
                                           Gst.PadPresence.ALWAYS,
                                           FIXED_CAPS))

    def do_set_caps(self, incaps, outcaps):
        struct = incaps.get_structure(0)
        self.width = struct.get_int("width").value
        self.height = struct.get_int("height").value
        return True

    def do_transform(self, buf_in, buf_out):
        try:
            # Create a read-only memoryview
            success, info = buf_in.map(Gst.MapFlags.READ)
            if success == False:
                raise RuntimeError("Could not map buffer to bytes object.")
            
            # Create a NumPy ndarray from the memoryview.
            image_in = np.ndarray(shape = (self.height, self.width), dtype = np.uint8, buffer = info.data)
            image_out = np.invert(image_in)

            # Copy data in image_out to buf_out
            buf_out.fill(0, image_out.tobytes())

            # Unmap the memoryview
            buf_in.unmap(info)

            return Gst.FlowReturn.OK
        except RuntimeError as e:
            Gst.error("Runtime error: %s" % e)
            return Gst.FlowReturn.ERROR

GObject.type_register(ExampleTransform)
__gstelementfactory__ = ("ExampleTransform", Gst.Rank.NONE, ExampleTransform)
