'''
Simple mixer element, accepts 320 x 240 RGBA at 30 fps
on any number of sinkpads.

Requires PIL (Python Imaging Library)

Example pipeline:

gst-launch-1.0 py_videomixer name=mixer ! videoconvert ! autovideosink \
        videotestsrc ! mixer. \
        videotestsrc pattern=ball ! mixer. \
        videotestsrc pattern=snow ! mixer.
'''

import gi

gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GObject', '2.0')
from gi.repository import Gst, GObject, GstBase

Gst.init_python()

try:
    from PIL import Image
except ImportError:
    Gst.error('py_videomixer requires PIL')
    raise

# Completely fixed input / output
ICAPS = Gst.Caps(Gst.Structure('video/x-raw',
                               format='RGBA',
                               width=320,
                               height=240,
                               framerate=Gst.Fraction(30, 1)))

OCAPS = Gst.Caps(Gst.Structure('video/x-raw',
                               format='RGBA',
                               width=320,
                               height=240,
                               framerate=Gst.Fraction(30, 1)))


class BlendData:
    def __init__(self, outimg):
        self.outimg = outimg
        self.pts = 0
        self.eos = True


class Videomixer(GstBase.Aggregator):
    __gstmetadata__ = ('Videomixer', 'Video/Mixer',
                       'Python video mixer', 'Mathieu Duponchelle')

    __gsttemplates__ = (
        Gst.PadTemplate.new_with_gtype("sink_%u",
                                       Gst.PadDirection.SINK,
                                       Gst.PadPresence.REQUEST,
                                       ICAPS,
                                       GstBase.AggregatorPad.__gtype__),
        Gst.PadTemplate.new_with_gtype("src",
                                       Gst.PadDirection.SRC,
                                       Gst.PadPresence.ALWAYS,
                                       OCAPS,
                                       GstBase.AggregatorPad.__gtype__)
    )

    def mix_buffers(self, agg, pad, bdata):
        buf = pad.pop_buffer()
        _, info = buf.map(Gst.MapFlags.READ)

        img = Image.frombuffer('RGBA', (320, 240), info.data, "raw", 'RGBA', 0, 1)

        bdata.outimg = Image.blend(bdata.outimg, img, alpha=0.5)
        bdata.pts = buf.pts

        # Need to ensure the PIL image has been released, or unmap will fail
        # with an outstanding memoryview buffer error
        del img

        buf.unmap(info)

        bdata.eos = False

        return True

    def do_aggregate(self, timeout):
        outimg = Image.new('RGBA', (320, 240), 0x00000000)

        bdata = BlendData(outimg)

        self.foreach_sink_pad(self.mix_buffers, bdata)

        data = bdata.outimg.tobytes()

        outbuf = Gst.Buffer.new_allocate(None, len(data), None)
        outbuf.fill(0, data)
        outbuf.pts = bdata.pts
        self.finish_buffer(outbuf)

        # We are EOS when no pad was ready to be aggregated,
        # this would obviously not work for live
        if bdata.eos:
            return Gst.FlowReturn.EOS

        return Gst.FlowReturn.OK


GObject.type_register(Videomixer)
__gstelementfactory__ = ("py_videomixer", Gst.Rank.NONE, Videomixer)
