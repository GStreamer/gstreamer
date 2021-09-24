# Playback tutorial 9: Digital audio pass-through

## Goal

This tutorial shows how GStreamer handles digital audio pass-through.

## Introduction

Besides the common analog format, high-end audio systems usually also
accept data in digital form, either compressed or uncompressed. This is
convenient because the audio signal then travels from the computer to
the speakers in a form that is more resilient to interference and noise,
resulting higher quality.

The connection is typically made through an
[S/PDIF](http://en.wikipedia.org/wiki/SPDIF) cable which can either be
optical (with [TOSLINK](http://en.wikipedia.org/wiki/TOSLINK)
connectors) or coaxial (with [RCA](http://en.wikipedia.org/wiki/RCA)
connectors). S/PDIF is also known as IEC 60958 type II (IEC 958 before
1998).

In this scenario, GStreamer does not need to perform audio decoding; it
can simply output the encoded data, acting in *pass-through* mode, and
let the external audio system perform the decoding.

## Inner workings of GStreamer audio sinks

First off, digital audio output must be enabled at the system level. The
method to achieve this depend on the operating system, but it generally
involves going to the audio control panel and activating a checkbox
reading “Digital Audio Output” or similar.

The main GStreamer audio sinks for each platform, Pulse Audio
(`pulsesink`) for Linux, `osxaudiosink` for OS X and Direct Sound
(`directsoundsink`) for Windows, detect when digital audio output is
available and change their input caps accordingly to accept encoded
data. For example, these elements typically accept `audio/x-raw` data:
when digital audio output is enabled in the system, they may also
accept `audio/mpeg`, `audio/x-ac3`, `audio/x-eac3` or `audio/x-dts`.

Then, when `playbin` builds the decoding pipeline, it realizes that the
audio sink can be directly connected to the encoded data (typically
coming out of a demuxer), so there is no need for a decoder. This
process is automatic and does not need any action from the application.

On Linux, there exist other audio sinks, like Alsa (`alsasink`) which
work differently (a “digital device” needs to be manually selected
through the `device` property of the sink). Pulse Audio, though, is the
commonly preferred audio sink on Linux.

## Precautions with digital formats

When Digital Audio Output is enabled at the system level, the GStreamer
audio sinks automatically expose all possible digital audio caps,
regardless of whether the actual audio decoder at the end of the S/PDIF
cable is able to decode all those formats. This is so because there is
no mechanism to query an external audio decoder which formats are
supported, and, in fact, the cable can even be disconnected during this
process.

For example, after enabling Digital Audio Output in the system’s Control
Panel,  `directsoundsink`  will automatically expose `audio/x-ac3`,
`audio/x-eac3` and `audio/x-dts` caps in addition to `audio/x-raw`.
However, one particular external decoder might only understand raw
integer streams and would try to play the compressed data as such (a
painful experience for your ears, rest assured).

Solving this issue requires user intervention, since only the user knows
the formats supported by the external decoder.

On some systems, the simplest solution is to inform the operating system
of the formats that the external audio decoder can accept. In this way,
the GStreamer audio sinks will only offer these formats. The acceptable
audio formats are commonly selected from the operating system’s audio
configuration panel, from the same place where Digital Audio Output is
enabled, but, unfortunately, this option is not available in all audio
drivers.

Another solution involves, using a custom sinkbin (see
[](tutorials/playback/custom-playbin-sinks.md)) which includes a
`capsfilter` element (see [](tutorials/basic/handy-elements.md))
and an audio sink. The caps that the external decoder supports are
then set in the capsfiler so the wrong format is not output. This
allows the application to enforce the appropriate format instead of
relying on the user to have the system correctly configured. Still
requires user intervention, but can be used regardless of the options
the audio driver offers.

Please do not use `autoaudiosink` as the audio sink, as it currently
only supports raw audio, and will ignore any compressed format.

## Conclusion

This tutorial has shown a bit of how GStreamer deals with digital audio.
In particular, it has shown that:

  - Applications using `playbin` do not need to do anything special to
    enable digital audio output: it is managed from the audio control
    panel of the operating system.

It has been a pleasure having you here, and see you soon!
