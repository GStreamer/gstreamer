'''
Element that generates a sine audio wave with the specified frequency

Requires numpy

Example pipeline:

gst-launch-1.0 py_audiotestsrc ! autoaudiosink
'''

import gi

gi.require_version('Gst', '1.0')
gi.require_version('GstBase', '1.0')
gi.require_version('GstAudio', '1.0')

from gi.repository import Gst, GLib, GObject, GstBase, GstAudio

try:
    import numpy as np
except ImportError:
    Gst.error('py_audiotestsrc requires numpy')
    raise

Gst.init_python()

OCAPS = Gst.Caps.from_string (
        'audio/x-raw, format=F32LE, layout=interleaved, rate=44100, channels=2')

SAMPLESPERBUFFER = 1024

DEFAULT_FREQ = 440
DEFAULT_VOLUME = 0.8
DEFAULT_MUTE = False
DEFAULT_IS_LIVE = False

class AudioTestSrc(GstBase.BaseSrc):
    __gstmetadata__ = ('CustomSrc','Src', \
                      'Custom test src element', 'Mathieu Duponchelle')

    __gproperties__ = {
        "freq": (int,
                 "Frequency",
                 "Frequency of test signal",
                 1,
                 GLib.MAXINT,
                 DEFAULT_FREQ,
                 GObject.ParamFlags.READWRITE
                ),
        "volume": (float,
                   "Volume",
                   "Volume of test signal",
                   0.0,
                   1.0,
                   DEFAULT_VOLUME,
                   GObject.ParamFlags.READWRITE
                  ),
        "mute": (bool,
                 "Mute",
                 "Mute the test signal",
                 DEFAULT_MUTE,
                 GObject.ParamFlags.READWRITE
                ),
        "is-live": (bool,
                 "Is live",
                 "Whether to act as a live source",
                 DEFAULT_IS_LIVE,
                 GObject.ParamFlags.READWRITE
                ),
    }

    __gsttemplates__ = Gst.PadTemplate.new("src",
                                           Gst.PadDirection.SRC,
                                           Gst.PadPresence.ALWAYS,
                                           OCAPS)

    def __init__(self):
        GstBase.BaseSrc.__init__(self)
        self.info = GstAudio.AudioInfo()

        self.freq = DEFAULT_FREQ
        self.volume = DEFAULT_VOLUME
        self.mute = DEFAULT_MUTE

        self.set_live(DEFAULT_IS_LIVE)
        self.set_format(Gst.Format.TIME)

    def do_set_caps(self, caps):
        self.info = GstAudio.AudioInfo.new_from_caps(caps)
        self.set_blocksize(self.info.bpf * SAMPLESPERBUFFER)
        return True

    def do_get_property(self, prop):
        if prop.name == 'freq':
            return self.freq
        elif prop.name == 'volume':
            return self.volume
        elif prop.name == 'mute':
            return self.mute
        elif prop.name == 'is-live':
            return self.is_live
        else:
            raise AttributeError('unknown property %s' % prop.name)

    def do_set_property(self, prop, value):
        if prop.name == 'freq':
            self.freq = value
        elif prop.name == 'volume':
            self.volume = value
        elif prop.name == 'mute':
            self.mute = value
        elif prop.name == 'is-live':
            self.set_live(value)
        else:
            raise AttributeError('unknown property %s' % prop.name)

    def do_start (self):
        self.next_sample = 0
        self.next_byte = 0
        self.next_time = 0
        self.accumulator = 0
        self.generate_samples_per_buffer = SAMPLESPERBUFFER

        return True

    def do_gst_base_src_query(self, query):
        if query.type == Gst.QueryType.LATENCY:
            latency = Gst.util_uint64_scale_int(self.generate_samples_per_buffer,
                    Gst.SECOND, self.info.rate)
            is_live = self.is_live
            query.set_latency(is_live, latency, Gst.CLOCK_TIME_NONE)
            res = True
        else:
            res = GstBase.BaseSrc.do_query(self, query)
        return res

    def do_get_times(self, buf):
        end = 0
        start = 0
        if self.is_live:
            ts = buf.pts
            if ts != Gst.CLOCK_TIME_NONE:
                duration = buf.duration
                if duration != Gst.CLOCK_TIME_NONE:
                    end = ts + duration
                start = ts
        else:
            start = Gst.CLOCK_TIME_NONE
            end = Gst.CLOCK_TIME_NONE

        return start, end

    def do_fill(self, offset, length, buf):
        if length == -1:
            samples = SAMPLESPERBUFFER
        else:
            samples = int(length / self.info.bpf)

        self.generate_samples_per_buffer = samples

        bytes_ = samples * self.info.bpf

        next_sample = self.next_sample + samples
        next_byte = self.next_byte + bytes_
        next_time = Gst.util_uint64_scale_int(next_sample, Gst.SECOND, self.info.rate)

        try:
            with buf.map(Gst.MapFlags.WRITE) as info:
                array = np.ndarray(shape = self.info.channels * samples, dtype = np.float32, buffer = info.data)
                if not self.mute:
                    r = np.repeat(np.arange(self.accumulator, self.accumulator + samples),
                            self.info.channels)
                    np.sin(2 * np.pi * r * self.freq / self.info.rate, out=array)
                    array *= self.volume
                else:
                    array[:] = 0
        except Exception as e:
            Gst.error("Mapping error: %s" % e)
            return (Gst.FlowReturn.ERROR, None)

        buf.offset = self.next_sample
        buf.offset_end = next_sample
        buf.pts = self.next_time
        buf.duration = next_time - self.next_time

        self.next_time = next_time
        self.next_sample = next_sample
        self.next_byte = next_byte
        self.accumulator += samples
        self.accumulator %= self.info.rate / self.freq

        return (Gst.FlowReturn.OK, buf)


__gstelementfactory__ = ("py_audiotestsrc", Gst.Rank.NONE, AudioTestSrc)
