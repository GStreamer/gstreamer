'''
Element that transforms audio samples to video frames representing
the waveform.

Requires matplotlib, numpy and numpy_ringbuffer

Example pipeline:

gst-launch-1.0 audiotestsrc ! audioplot window-duration=0.01 ! videoconvert ! autovideosink
'''

import gi

gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GstAudio', '1.0')
gi.require_version('GstVideo', '1.0')
from gi.repository import Gst, GLib, GObject, GstBase, GstAudio, GstVideo

try:
    import numpy as np
    import matplotlib.patheffects as pe
    from numpy_ringbuffer import RingBuffer
    from matplotlib import pyplot as plt
    from matplotlib.backends.backend_agg import FigureCanvasAgg
except ImportError:
    Gst.error('audioplot requires numpy, numpy_ringbuffer and matplotlib')
    raise


Gst.init_python()

AUDIO_FORMATS = [f.strip() for f in
                 GstAudio.AUDIO_FORMATS_ALL.strip('{ }').split(',')]

ICAPS = Gst.Caps(Gst.Structure('audio/x-raw',
                               format=Gst.ValueList(AUDIO_FORMATS),
                               layout='interleaved',
                               rate=Gst.IntRange(range(1, GLib.MAXINT)),
                               channels=Gst.IntRange(range(1, GLib.MAXINT))))

OCAPS = Gst.Caps(Gst.Structure('video/x-raw',
                               format='ARGB',
                               width=Gst.IntRange(range(1, GLib.MAXINT)),
                               height=Gst.IntRange(range(1, GLib.MAXINT)),
                               framerate=Gst.FractionRange(Gst.Fraction(1, 1),
                                                           Gst.Fraction(GLib.MAXINT, 1))))

DEFAULT_WINDOW_DURATION = 1.0
DEFAULT_WIDTH = 640
DEFAULT_HEIGHT = 480
DEFAULT_FRAMERATE_NUM = 25
DEFAULT_FRAMERATE_DENOM = 1


class AudioPlotFilter(GstBase.BaseTransform):
    __gstmetadata__ = ('AudioPlotFilter', 'Filter',
                       'Plot audio waveforms', 'Mathieu Duponchelle')

    __gsttemplates__ = (Gst.PadTemplate.new("src",
                                            Gst.PadDirection.SRC,
                                            Gst.PadPresence.ALWAYS,
                                            OCAPS),
                        Gst.PadTemplate.new("sink",
                                            Gst.PadDirection.SINK,
                                            Gst.PadPresence.ALWAYS,
                                            ICAPS))
    __gproperties__ = {
        "window-duration": (float,
                            "Window Duration",
                            "Duration of the sliding window, in seconds",
                            0.01,
                            100.0,
                            DEFAULT_WINDOW_DURATION,
                            GObject.ParamFlags.READWRITE
                            )
    }

    def __init__(self):
        GstBase.BaseTransform.__init__(self)
        self.window_duration = DEFAULT_WINDOW_DURATION

    def do_get_property(self, prop):
        if prop.name == 'window-duration':
            return self.window_duration
        else:
            raise AttributeError('unknown property %s' % prop.name)

    def do_set_property(self, prop, value):
        if prop.name == 'window-duration':
            self.window_duration = value
        else:
            raise AttributeError('unknown property %s' % prop.name)

    def do_transform(self, inbuf, outbuf):
        if not self.h:
            self.h, = self.ax.plot(np.array(self.ringbuffer),
                                   lw=0.5,
                                   color='k',
                                   path_effects=[pe.Stroke(linewidth=1.0,
                                                           foreground='g'),
                                                 pe.Normal()])
        else:
            self.h.set_ydata(np.array(self.ringbuffer))

        self.fig.canvas.restore_region(self.background)
        self.ax.draw_artist(self.h)
        self.fig.canvas.blit(self.ax.bbox)

        s = self.agg.tostring_argb()

        outbuf.fill(0, s)
        outbuf.pts = self.next_time
        outbuf.duration = self.frame_duration

        self.next_time += self.frame_duration

        return Gst.FlowReturn.OK

    def __append(self, data):
        arr = np.array(data)
        end = self.thinning_factor * int(len(arr) / self.thinning_factor)
        arr = np.mean(arr[:end].reshape(-1, self.thinning_factor), 1)
        self.ringbuffer.extend(arr)

    def do_generate_output(self):
        inbuf = self.queued_buf
        _, info = inbuf.map(Gst.MapFlags.READ)
        res, data = self.converter.convert(GstAudio.AudioConverterFlags.NONE,
                                           info.data)
        data = memoryview(data).cast('i')

        nsamples = len(data) - self.buf_offset

        if nsamples == 0:
            self.buf_offset = 0
            inbuf.unmap(info)
            return Gst.FlowReturn.OK, None

        if self.cur_offset + nsamples < self.next_offset:
            self.__append(data[self.buf_offset:])
            self.buf_offset = 0
            self.cur_offset += nsamples
            inbuf.unmap(info)
            return Gst.FlowReturn.OK, None

        consumed = self.next_offset - self.cur_offset

        self.__append(data[self.buf_offset:self.buf_offset + consumed])
        inbuf.unmap(info)

        _, outbuf = GstBase.BaseTransform.do_prepare_output_buffer(self, inbuf)

        ret = self.do_transform(inbuf, outbuf)

        self.next_offset += self.samplesperbuffer

        self.cur_offset += consumed
        self.buf_offset += consumed

        return ret, outbuf

    def do_transform_caps(self, direction, caps, filter_):
        if direction == Gst.PadDirection.SRC:
            res = ICAPS
        else:
            res = OCAPS

        if filter_:
            res = res.intersect(filter_)

        return res

    def do_fixate_caps(self, direction, caps, othercaps):
        if direction == Gst.PadDirection.SRC:
            return othercaps.fixate()
        else:
            so = othercaps.get_structure(0).copy()
            so.fixate_field_nearest_fraction("framerate",
                                             DEFAULT_FRAMERATE_NUM,
                                             DEFAULT_FRAMERATE_DENOM)
            so.fixate_field_nearest_int("width", DEFAULT_WIDTH)
            so.fixate_field_nearest_int("height", DEFAULT_HEIGHT)
            ret = Gst.Caps.new_empty()
            ret.append_structure(so)
            return ret.fixate()

    def do_set_caps(self, icaps, ocaps):
        in_info = GstAudio.AudioInfo.new_from_caps(icaps)
        out_info = GstVideo.VideoInfo().new_from_caps(ocaps)

        self.convert_info = GstAudio.AudioInfo()
        self.convert_info.set_format(GstAudio.AudioFormat.S32,
                                     in_info.rate,
                                     in_info.channels,
                                     in_info.position)
        self.converter = GstAudio.AudioConverter.new(GstAudio.AudioConverterFlags.NONE,
                                                     in_info,
                                                     self.convert_info,
                                                     None)

        self.fig = plt.figure()
        dpi = self.fig.get_dpi()
        self.fig.patch.set_alpha(0.3)
        self.fig.set_size_inches(out_info.width / float(dpi),
                                 out_info.height / float(dpi))
        self.ax = plt.Axes(self.fig, [0., 0., 1., 1.])
        self.fig.add_axes(self.ax)
        self.ax.set_axis_off()
        self.ax.set_ylim((GLib.MININT, GLib.MAXINT))
        self.agg = self.fig.canvas.switch_backends(FigureCanvasAgg)
        self.h = None

        samplesperwindow = int(in_info.rate * in_info.channels * self.window_duration)
        self.thinning_factor = max(int(samplesperwindow / out_info.width - 1), 1)

        cap = int(samplesperwindow / self.thinning_factor)
        self.ax.set_xlim([0, cap])
        self.ringbuffer = RingBuffer(capacity=cap)
        self.ringbuffer.extend([0.0] * cap)
        self.frame_duration = Gst.util_uint64_scale_int(Gst.SECOND,
                                                        out_info.fps_d,
                                                        out_info.fps_n)
        self.next_time = self.frame_duration

        self.agg.draw()
        self.background = self.fig.canvas.copy_from_bbox(self.ax.bbox)

        self.samplesperbuffer = Gst.util_uint64_scale_int(in_info.rate * in_info.channels,
                                                          out_info.fps_d,
                                                          out_info.fps_n)
        self.next_offset = self.samplesperbuffer
        self.cur_offset = 0
        self.buf_offset = 0

        return True


GObject.type_register(AudioPlotFilter)
__gstelementfactory__ = ("audioplot", Gst.Rank.NONE, AudioPlotFilter)
