# Basic tutorial 10: GStreamer tools

## Goal

GStreamer comes with a set of tools which range from handy to
absolutely essential. There is no code in this tutorial, just sit back
and relax, and we will teach you:

  - How to build and run GStreamer pipelines from the command line,
    without using C at all!
  - How to find out what GStreamer elements you have available and their
    capabilities.
  - How to discover the internal structure of media files.

## Introduction

These tools are available in the bin directory of the GStreamer
binaries. You need to move to this directory to execute them, because
it is not added to the system’s `PATH` environment variable (to avoid
polluting it too much).

Just open a terminal (or console window) and go to the `bin` directory
of your GStreamer installation (Read again the [Installing
GStreamer](installing/index.md) section to find out where this is),
and you are ready to start typing the commands given in this tutorial.


> ![Information](images/icons/emoticons/information.svg)
>
> On Linux, you should use the GStreamer version installed with your
> distribution, the tools should be installed with a package named `gstreamer1`
> on Fedora style distributions, or `gstreamer1.0-tools` on Debian/Ubuntu style
> distributions.

In order to allow for multiple versions of GStreamer to coexists in the
same system, these tools are versioned, this is, a GStreamer version
number is appended to their name. This version is based on
GStreamer 1.0, so the tools are called `gst-launch-1.0`,
`gst-inspect-1.0` and `gst-discoverer-1.0`

## `gst-launch-1.0`

This tool accepts a textual description of a pipeline, instantiates it,
and sets it to the PLAYING state. It allows you to quickly check if a
given pipeline works, before going through the actual implementation
using GStreamer API calls.

Bear in mind that it can only create simple pipelines. In particular, it
can only simulate the interaction of the pipeline with the application
up to a certain level. In any case, it is extremely handy to test
pipelines quickly, and is used by GStreamer developers around the world
on a daily basis.

Please note that `gst-launch-1.0` is primarily a debugging tool for
developers. You should not build applications on top of it. Instead, use
the `gst_parse_launch()` function of the GStreamer API as an easy way to
construct pipelines from pipeline descriptions.

Although the rules to construct pipeline descriptions are very simple,
the concatenation of multiple elements can quickly make such
descriptions resemble black magic. Fear not, for everyone learns the
`gst-launch-1.0` syntax, eventually.

The command line for gst-launch-1.0 consists of a list of options followed
by a PIPELINE-DESCRIPTION. Some simplified instructions are given next,
see the complete documentation at [the reference page](tools/gst-launch.md)
for `gst-launch-1.0`.

### Elements

In simple form, a PIPELINE-DESCRIPTION is a list of element types
separated by exclamation marks (!). Go ahead and type in the following
command:

```
gst-launch-1.0 videotestsrc ! videoconvert ! autovideosink
```

You should see a windows with an animated video pattern. Use CTRL+C on
the terminal to stop the program.

This instantiates a new element of type `videotestsrc` (an element which
generates a sample video pattern), an `videoconvert` (an element
which does raw video format conversion, making sure other elements can
understand each other), and an `autovideosink` (a window to which video
is rendered). Then, GStreamer tries to link the output of each element
to the input of the element appearing on its right in the description.
If more than one input or output Pad is available, the Pad Caps are used
to find two compatible Pads.

### Properties

Properties may be appended to elements, in the form
*property=value *(multiple properties can be specified, separated by
spaces). Use the `gst-inspect-1.0` tool (explained next) to find out the
available properties for an
element.

```
gst-launch-1.0 videotestsrc pattern=11 ! videoconvert ! autovideosink
```

You should see a static video pattern, made of circles.

### Named elements

Elements can be named using the `name` property, in this way complex
pipelines involving branches can be created. Names allow linking to
elements created previously in the description, and are indispensable to
use elements with multiple output pads, like demuxers or tees, for
example.

Named elements are referred to using their name followed by a
dot.

```
gst-launch-1.0 videotestsrc ! videoconvert ! tee name=t ! queue ! autovideosink t. ! queue ! autovideosink
```

You should see two video windows, showing the same sample video pattern.
If you see only one, try to move it, since it is probably on top of the
second window.

This example instantiates a `videotestsrc`, linked to a
`videoconvert`, linked to a `tee` (Remember from [](tutorials/basic/multithreading-and-pad-availability.md) that
a `tee` copies to each of its output pads everything coming through its
input pad). The `tee` is named simply ‘t’ (using the `name` property)
and then linked to a `queue` and an `autovideosink`. The same `tee` is
referred to using ‘t.’ (mind the dot) and then linked to a second
`queue` and a second `autovideosink`.

To learn why the queues are necessary read [](tutorials/basic/multithreading-and-pad-availability.md).

### Pads

Instead of letting GStreamer choose which Pad to use when linking two
elements, you may want to specify the Pads directly. You can do this by
adding a dot plus the Pad name after the name of the element (it must be
a named element). Learn the names of the Pads of an element by using
the `gst-inspect-1.0` tool.

This is useful, for example, when you want to retrieve one particular
stream out of a
demuxer:

```
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux name=d d.video_0 ! matroskamux ! filesink location=sintel_video.mkv
```

This fetches a media file from the internet using `souphttpsrc`, which
is in webm format (a special kind of Matroska container, see [](tutorials/basic/concepts.md)). We
then open the container using `matroskademux`. This media contains both
audio and video, so `matroskademux` will create two output Pads, named
`video_0` and `audio_0`. We link `video_0` to a `matroskamux` element
to re-pack the video stream into a new container, and finally link it to
a `filesink`, which will write the stream into a file named
"sintel\_video.mkv" (the `location` property specifies the name of the
file).

All in all, we took a webm file, stripped it of audio, and generated a
new matroska file with the video. If we wanted to keep only the
audio:

```
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux name=d d.audio_0 ! vorbisparse ! matroskamux ! filesink location=sintel_audio.mka
```

The `vorbisparse` element is required to extract some information from
the stream and put it in the Pad Caps, so the next element,
`matroskamux`, knows how to deal with the stream. In the case of video
this was not necessary, because `matroskademux` already extracted this
information and added it to the Caps.

Note that in the above two examples no media has been decoded or played.
We have just moved from one container to another (demultiplexing and
re-multiplexing again).

### Caps filters

When an element has more than one output pad, it might happen that the
link to the next element is ambiguous: the next element may have more
than one compatible input pad, or its input pad may be compatible with
the Pad Caps of all the output pads. In these cases GStreamer will link
using the first pad that is available, which pretty much amounts to
saying that GStreamer will choose one output pad at random.

Consider the following
pipeline:

```
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux ! filesink location=test
```

This is the same media file and demuxer as in the previous example. The
input Pad Caps of `filesink` are `ANY`, meaning that it can accept any
kind of media. Which one of the two output pads of `matroskademux` will
be linked against the filesink? `video_0` or `audio_0`? You cannot
know.

You can remove this ambiguity, though, by using named pads, as in the
previous sub-section, or by using **Caps
Filters**:

```
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux ! video/x-vp8 ! matroskamux ! filesink location=sintel_video.mkv
```

A Caps Filter behaves like a pass-through element which does nothing and
only accepts media with the given Caps, effectively resolving the
ambiguity. In this example, between `matroskademux` and `matroskamux` we
added a `video/x-vp8` Caps Filter to specify that we are interested in
the output pad of `matroskademux` which can produce this kind of video.

To find out the Caps an element accepts and produces, use the
`gst-inspect-1.0` tool. To find out the Caps contained in a particular file,
use the `gst-discoverer-1.0` tool. To find out the Caps an element is
producing for a particular pipeline, run `gst-launch-1.0` as usual, with the
`–v` option to print Caps information.

### Examples

Play a media file using `playbin` (as in [](tutorials/basic/hello-world.md)):

```
gst-launch-1.0 playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm
```

A fully operation playback pipeline, with audio and video (more or less
the same pipeline that `playbin` will create
internally):

```
gst-launch-1.0 souphttpsrc location=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! matroskademux name=d ! queue ! vp8dec ! videoconvert ! autovideosink d. ! queue ! vorbisdec ! audioconvert ! audioresample ! autoaudiosink
```

A transcoding pipeline, which opens the webm container and decodes both
streams (via uridecodebin), then re-encodes the audio and video branches
with a different codec, and puts them back together in an Ogg container
(just for the sake of
it).

```
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm name=d ! queue ! theoraenc ! oggmux name=m ! filesink location=sintel.ogg d. ! queue ! audioconvert ! audioresample ! flacenc ! m.
```

A rescaling pipeline. The `videoscale` element performs a rescaling
operation whenever the frame size is different in the input and the
output caps. The output caps are set by the Caps Filter to
320x200.

```
gst-launch-1.0 uridecodebin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm ! queue ! videoscale ! video/x-raw-yuv,width=320,height=200 ! videoconvert ! autovideosink
```

This short description of `gst-launch-1.0` should be enough to get you
started. Remember that you have the [complete documentation available
here](tools/gst-launch.md).

## `gst-inspect-1.0`

This tool has three modes of operation:

  - Without arguments, it lists all available elements types, this is,
    the types you can use to instantiate new elements.
  - With a file name as an argument, it treats the file as a GStreamer
    plugin, tries to open it, and lists all the elements described
    inside.
  - With a GStreamer element name as an argument, it lists all
    information regarding that element.

Let's see an example of the third mode:

```
gst-inspect-1.0 vp8dec

Factory Details:
  Rank                     primary (256)
  Long-name                On2 VP8 Decoder
  Klass                    Codec/Decoder/Video
  Description              Decode VP8 video streams
  Author                   David Schleef <ds@entropywave.com>, Sebastian Dröge <sebastian.droege@collabora.co.uk>

Plugin Details:
  Name                     vpx
  Description              VP8 plugin
  Filename                 /usr/lib64/gstreamer-1.0/libgstvpx.so
  Version                  1.6.4
  License                  LGPL
  Source module            gst-plugins-good
  Source release date      2016-04-14
  Binary package           Fedora GStreamer-plugins-good package
  Origin URL               http://download.fedoraproject.org

GObject
 +----GInitiallyUnowned
       +----GstObject
             +----GstElement
                   +----GstVideoDecoder
                         +----GstVP8Dec

Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/x-vp8

  SRC template: 'src'
    Availability: Always
    Capabilities:
      video/x-raw
                 format: I420
                  width: [ 1, 2147483647 ]
                 height: [ 1, 2147483647 ]
              framerate: [ 0/1, 2147483647/1 ]


Element Flags:
  no flags set

Element Implementation:
  Has change_state() function: gst_video_decoder_change_state

Element has no clocking capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Pad Template: 'sink'
  SRC: 'src'
    Pad Template: 'src'

Element Properties:
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "vp8dec0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  post-processing     : Enable post processing
                        flags: readable, writable
                        Boolean. Default: false
  post-processing-flags: Flags to control post processing
                        flags: readable, writable
                        Flags "GstVP8DecPostProcessingFlags" Default: 0x00000403, "mfqe+demacroblock+deblock"
                           (0x00000001): deblock          - Deblock
                           (0x00000002): demacroblock     - Demacroblock
                           (0x00000004): addnoise         - Add noise
                           (0x00000400): mfqe             - Multi-frame quality enhancement
  deblocking-level    : Deblocking level
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 16 Default: 4
  noise-level         : Noise level
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 16 Default: 0
  threads             : Maximum number of decoding threads
                        flags: readable, writable
                        Unsigned Integer. Range: 1 - 16 Default: 0
```

The most relevant sections are:

  - Pad Templates: This lists all the kinds of Pads this
    element can have, along with their capabilities. This is where you
    look to find out if an element can link with another one. In this
    case, it has only one sink pad template, accepting only
    `video/x-vp8` (encoded video data in VP8 format) and only one source
    pad template, producing `video/x-raw` (decoded video data).
  - Element Properties: This lists the properties of the
    element, along with their type and accepted values.

For more information, you can check the [documentation
page](tools/gst-inspect.md) of `gst-inspect-1.0`.

## `gst-discoverer-1.0`

This tool is a wrapper around the `GstDiscoverer` object shown in [](tutorials/basic/media-information-gathering.md).
It accepts a URI from the command line and prints all information
regarding the media that GStreamer can extract. It is useful to find out
what container and codecs have been used to produce the media, and
therefore what elements you need to put in a pipeline to play it.

Use `gst-discoverer-1.0 --help` to obtain the list of available options,
which basically control the amount of verbosity of the output.

Let's see an
example:

```
gst-discoverer-1.0 https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm -v

Analyzing https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm
Done discovering https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm
Topology:
  container: video/webm
    audio: audio/x-vorbis, channels=(int)2, rate=(int)48000
      Codec:
        audio/x-vorbis, channels=(int)2, rate=(int)48000
      Additional info:
        None
      Language: en
      Channels: 2
      Sample rate: 48000
      Depth: 0
      Bitrate: 80000
      Max bitrate: 0
      Tags:
        taglist, language-code=(string)en, container-format=(string)Matroska, audio-codec=(string)Vorbis, application-name=(string)ffmpeg2theora-0.24, encoder=(string)"Xiph.Org\ libVorbis\ I\ 20090709", encoder-version=(uint)0, nominal-bitrate=(uint)80000, bitrate=(uint)80000;
    video: video/x-vp8, width=(int)854, height=(int)480, framerate=(fraction)25/1
      Codec:
        video/x-vp8, width=(int)854, height=(int)480, framerate=(fraction)25/1
      Additional info:
        None
      Width: 854
      Height: 480
      Depth: 0
      Frame rate: 25/1
      Pixel aspect ratio: 1/1
      Interlaced: false
      Bitrate: 0
      Max bitrate: 0
      Tags:
        taglist, video-codec=(string)"VP8\ video", container-format=(string)Matroska;

Properties:
  Duration: 0:00:52.250000000
  Seekable: yes
  Tags:
      video codec: VP8 video
      language code: en
      container format: Matroska
      application name: ffmpeg2theora-0.24
      encoder: Xiph.Org libVorbis I 20090709
      encoder version: 0
      audio codec: Vorbis
      nominal bitrate: 80000
      bitrate: 80000
```

## Conclusion

This tutorial has shown:

  - How to build and run GStreamer pipelines from the command line using
    the `gst-launch-1.0` tool.
  - How to find out what GStreamer elements you have available and their
    capabilities, using the `gst-inspect-1.0` tool.
  - How to discover the internal structure of media files, using
    `gst-discoverer-1.0`.

It has been a pleasure having you here, and see you soon!
