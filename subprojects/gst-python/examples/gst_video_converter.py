#!/usr/bin/env python3
# Demonstration of using compositor and the samples-selected
# signal to do frame-by-frame updates and animation by
# udpating compositor pad properties and the GstVideoConverter
# config.
#
# Supply a URI argument to use a video file in the example,
# or omit it to just animate a videotestsrc.
import gi
import math
import sys

gi.require_version('Gst', '1.0')
gi.require_version('GstVideo', '1.0')
gi.require_version('GLib', '2.0')
gi.require_version('GObject', '2.0')
from gi.repository import GLib, GObject, Gst, GstVideo


def bus_call(bus, message, loop):
    t = message.type
    if t == Gst.MessageType.EOS:
        sys.stdout.write("End-of-stream\n")
        loop.quit()
    elif t == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        sys.stderr.write("Error: %s: %s\n" % (err, debug))
        loop.quit()
    return True


def update_compositor(comp, seg, pts, dts, duration, info):
    sink = comp.get_static_pad("sink_1")
    # Can peek at the input sample(s) here to get the real video input caps
    sample = comp.peek_next_sample(sink)
    caps = sample.get_caps()
    in_w = caps[0]['width']
    in_h = caps[0]['height']

    dest_w = 160 + in_w * math.fabs(math.sin(pts / (10 * Gst.SECOND) * math.pi))
    dest_h = 120 + in_h * math.fabs(math.sin(pts / (11 * Gst.SECOND) * math.pi))
    x = (1920 - dest_w) * math.fabs(math.sin(pts / (5 * Gst.SECOND) * math.pi))
    y = (1080 - dest_h) * math.fabs(math.sin(pts / (7 * Gst.SECOND) * math.pi))

    sink.set_property("xpos", x)
    sink.set_property("ypos", y)
    sink.set_property("width", dest_w)
    sink.set_property("height", dest_h)

    # Update video-converter settings
    cfg = Gst.Structure.new_empty("GstVideoConverter")

    # When scaling down, switch to nearest-neighbour scaling, otherwise use bilinear
    if in_w < dest_w or in_h < dest_h:
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_RESAMPLER_METHOD, GstVideo.VideoResamplerMethod.NEAREST)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_CHROMA_RESAMPLER_METHOD, GstVideo.VideoResamplerMethod.NEAREST)
    else:
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_RESAMPLER_METHOD, GstVideo.VideoResamplerMethod.LINEAR)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_CHROMA_RESAMPLER_METHOD, GstVideo.VideoResamplerMethod.LINEAR)

    # Crop some from the top and bottom or sides of the source image
    crop = 64 * math.cos(pts / (4 * Gst.SECOND) * math.pi)
    if crop < 0:
        crop = min(in_w / 2, -crop)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_SRC_X, int(crop))
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_SRC_WIDTH, int(in_w - 2 * crop))
    else:
        crop = min(in_h / 2, crop)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_SRC_Y, int(crop))
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_SRC_HEIGHT, int(in_h - 2 * crop))

    # Add add some borders to the result by not filling the destination rect
    border = 64 * math.sin(pts / (4 * Gst.SECOND) * math.pi)
    if border < 0:
        border = min(in_w / 2, -border)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DEST_X, int(border))
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DEST_WIDTH, int(dest_w - 2 * border))
    else:
        border = min(in_h / 2, border)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DEST_Y, int(border))
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DEST_HEIGHT, int(dest_h - 2 * border))

    # Set the border colour. Need to set this into a GValue explicitly to ensure it's a uint
    argb = GObject.Value()
    argb.init(GObject.TYPE_UINT)
    argb.set_uint(0x80FF80FF)
    cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_BORDER_ARGB, argb)
    cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_FILL_BORDER, True)

    # When things get wide, do some dithering why not
    if dest_w > in_w:
        # Dither quantization must also be a uint
        dither = GObject.Value()
        dither.init(GObject.TYPE_UINT)
        dither.set_uint(64)

        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DITHER_METHOD, GstVideo.VideoDitherMethod.FLOYD_STEINBERG)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DITHER_QUANTIZATION, dither)
    else:
        dither = GObject.Value()
        dither.init(GObject.TYPE_UINT)
        dither.set_uint(1)

        # cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DITHER_METHOD, GstVideo.VideoDitherMethod.NONE)
        cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_DITHER_QUANTIZATION, dither)

    # and fade it in and out every 13 seconds
    alpha = 0.25 + 0.75 * math.fabs(math.sin(pts / (13 * Gst.SECOND) * math.pi))
    # Use the video-converter to render alpha
    # cfg.set_value(GstVideo.VIDEO_CONVERTER_OPT_ALPHA_VALUE, alpha)
    # or use the pad's alpha setting is better because compositor can use it to skip rendering
    sink.set_property("alpha", alpha)

    # print(f"Setting config {cfg}")
    sink.set_property("converter-config", cfg)


if __name__ == "__main__":
    Gst.init(sys.argv)

    pipeline = Gst.ElementFactory.make('pipeline', None)

    compositor = Gst.ElementFactory.make("compositor", None)
    compositor.set_property("emit-signals", True)
    compositor.set_property("background", "black")
    compositor.connect("samples-selected", update_compositor)

    cf = Gst.ElementFactory.make("capsfilter", None)
    # Need to make sure the output has RGBA or AYUV, or we can't do alpha blending / fades:
    caps = Gst.Caps.from_string("video/x-raw,width=1920,height=1080,framerate=25/1,format=RGBA")
    cf.set_property("caps", caps)

    conv = Gst.ElementFactory.make("videoconvert", None)
    sink = Gst.ElementFactory.make("autovideosink", None)

    pipeline.add(compositor, cf, conv, sink)
    Gst.Element.link_many(compositor, cf, conv, sink)

    bgsource = Gst.parse_bin_from_description("videotestsrc pattern=circular ! capsfilter name=cf", False)

    cfsrc = bgsource.get_by_name("cf")
    caps = Gst.Caps.from_string("video/x-raw,width=320,height=180,framerate=1/1,format=RGB")
    cfsrc.set_property("caps", caps)
    src = cfsrc.get_static_pad("src")
    bgsource.add_pad(Gst.GhostPad.new("src", src))

    pipeline.add(bgsource)
    bgsource.link(compositor)

    pad = compositor.get_static_pad("sink_0")
    pad.set_property("width", 1920)
    pad.set_property("height", 1080)

    if len(sys.argv) > 1:
        source = Gst.parse_bin_from_description("uridecodebin name=u ! capsfilter name=cf caps=video/x-raw", False)
        u = source.get_by_name("u")
        u.set_property("uri", sys.argv[1])

        cfsrc = source.get_by_name("cf")
        caps = Gst.Caps.from_string("video/x-raw")
        cfsrc.set_property("caps", caps)

        # Expose the capsfilter source pad as a ghost pad
        src = cfsrc.get_static_pad("src")
        source.add_pad(Gst.GhostPad.new("src", src))
    else:
        source = Gst.parse_bin_from_description("videotestsrc ! capsfilter name=cf", False)
        cfsrc = source.get_by_name("cf")
        caps = Gst.Caps.from_string("video/x-raw,width=320,height=240,framerate=30/1,format=I420")
        cfsrc.set_property("caps", caps)

        src = cfsrc.get_static_pad("src")
        source.add_pad(Gst.GhostPad.new("src", src))

    pipeline.add(source)
    source.link(compositor)

    pipeline.set_state(Gst.State.PLAYING)

    bus = pipeline.get_bus()
    bus.add_signal_watch()

    loop = GLib.MainLoop()
    bus.connect("message", bus_call, loop)
    loop.run()

    pipeline.set_state(Gst.State.NULL)
    pipeline.get_state(Gst.CLOCK_TIME_NONE)
