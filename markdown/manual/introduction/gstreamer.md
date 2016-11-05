---
title: What is GStreamer?
...

# What is GStreamer?

GStreamer is a framework for creating streaming media applications. The
fundamental design comes from the video pipeline at Oregon Graduate
Institute, as well as some ideas from DirectShow.

GStreamer's development framework makes it possible to write any type of
streaming multimedia application. The GStreamer framework is designed to
make it easy to write applications that handle audio or video or both.
It isn't restricted to audio and video, and can process any kind of data
flow. The pipeline design is made to have little overhead above what the
applied filters induce. This makes GStreamer a good framework for
designing even high-end audio applications which put high demands on
latency.

One of the most obvious uses of GStreamer is using it to build a media
player. GStreamer already includes components for building a media
player that can support a very wide variety of formats, including MP3,
Ogg/Vorbis, MPEG-1/2, AVI, Quicktime, mod, and more. GStreamer, however,
is much more than just another media player. Its main advantages are
that the pluggable components can be mixed and matched into arbitrary
pipelines so that it's possible to write a full-fledged video or audio
editing application.

The framework is based on plugins that will provide the various codec
and other functionality. The plugins can be linked and arranged in a
pipeline. This pipeline defines the flow of the data. Pipelines can also
be edited with a GUI editor and saved as XML so that pipeline libraries
can be made with a minimum of effort.

The GStreamer core function is to provide a framework for plugins, data
flow and media type handling/negotiation. It also provides an API to
write applications using the various plugins.

Specifically, GStreamer provides

  - an API for multimedia applications

  - a plugin architecture

  - a pipeline architecture

  - a mechanism for media type handling/negotiation

  - a mechanism for synchronization

  - over 250 plug-ins providing more than 1000 elements

  - a set of tools

GStreamer plug-ins could be classified into

  - protocols handling

  - sources: for audio and video (involves protocol plugins)

  - formats: parsers, formaters, muxers, demuxers, metadata, subtitles

  - codecs: coders and decoders

  - filters: converters, mixers, effects, ...

  - sinks: for audio and video (involves protocol plugins)

![GStreamer overview](images/gstreamer-overview.png "fig:")

GStreamer is packaged into

  - gstreamer: the core package

  - gst-plugins-base: an essential exemplary set of elements

  - gst-plugins-good: a set of good-quality plug-ins under LGPL

  - gst-plugins-ugly: a set of good-quality plug-ins that might pose
    distribution problems

  - gst-plugins-bad: a set of plug-ins that need more quality

  - gst-libav: a set of plug-ins that wrap libav for decoding and
    encoding

  - a few others packages
