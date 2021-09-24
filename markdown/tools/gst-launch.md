# gst-launch-1.0

> ![information] This content comes mostly from the Linux man page for
> the `gst-launch-1.0` tool. As such, it is very Linux-centric
> regarding path specification and plugin names. Please be patient while
> it is rewritten to be more generic.

## Name

`gst-launch-1.0` - build and run a GStreamer pipeline

## Synopsis

```
gst-launch-1.0 [OPTIONS] PIPELINE-DESCRIPTION
```

## Description

`gst-launch-1.0` is a tool that builds and runs basic *GStreamer* pipelines.

In its simplest form, a PIPELINE-DESCRIPTION is a list of elements separated
by exclamation marks (!). Properties may be appended to elements in the
form `property=value`.

For a more complete description of possible PIPELINE-DESCRIPTIONS see the
section [Pipeline Description](#pipeline-description) below or consult the GStreamer documentation.

Please note that `gst-launch-1.0` is primarily a debugging tool. You should
not build applications on top of it. For applications, use the
`gst_parse_launch()` function of the GStreamer API as an easy way to construct
pipelines from pipeline descriptions.

## Options

*gst-launch-1.0* accepts the following options:

**--help**

Print help synopsis and available FLAGS

**-v, --verbose**

Output status information and property notifications

**-q, --quiet**

Do not print any progress information

**-m, --messages**

Output messages posted on the pipeline's bus

**-t, --tags**

Output tags (also known as metadata)

**-o FILE, --output=FILE**

Save XML representation of pipeline to FILE and exit

**-f, --no\_fault**

Do not install a fault handler

**-T, --trace**

Print memory allocation traces. The feature must be enabled at compile
time to work.

## GStreamer Options

`gst-launch-1.0` also accepts the following options that are common to
all GStreamer applications:

## Pipeline Description

A pipeline consists of *elements* and *links*. *Elements* can be put
into *bins* of different sorts. *Elements*, *links*, and *bins* can be
specified in a pipeline description in any order.

### Elements

```
ELEMENTTYPE [PROPERTY1 ...]
```

Creates an element of type `ELEMENTTYPE` and sets its `PROPERTIES`.

### Element Properties

```
PROPERTY=VALUE ...
```

Sets the property to the specified value. You can use `gst-inspect-1.0` to find
out about properties and allowed values of different elements. Enumeration
properties can be set by name, nick or value.

### Bins

```
[BINTYPE.] ([PROPERTY1 ...] PIPELINE-DESCRIPTION)
```

Specifies that a bin of type `BINTYPE` is created and the given properties
are set. Every element between the braces is put into the bin. Please
note the dot that has to be used after the `BINTYPE`. You will almost
never need this functionality, it is only really useful for applications
using the `gst_parse_launch()` API with `bin` as bintype. That way it is
possible to build partial pipelines instead of a full-fledged top-level
pipeline.

### Links

```
[[SRCELEMENT\].[PAD1,...]] ! [[SINKELEMENT].[PAD1,...]]
```

Links the element with name SRCELEMENT to the element with name SINKELEMENT.
Names can be set on elements using the `name` property. If the name is omitted,
the element that was specified directly in front of or after the link is
used. This works across bins. If a padname is given, the link is done using that
pad. If no pad names are given all possibilities are tried and a compatible pad
is used. If multiple padnames are given, both sides must have the same number of
pads specified and multiple links are done in the given order. The simplest link
is a simple exclamation mark. This links the element to the left of it with the
element at its right.


The following links the element with name SRCELEMENT to the element with name
SINKELEMENT, using the caps specified in CAPS as a filter:

```
[[SRCELEMENT].[PAD1,...]] ! CAPS ! [[SINKELEMENT].[PAD1,...]]
```

### Caps

```
MIMETYPE [, PROPERTY[, PROPERTY ...]]] [; CAPS[; CAPS ...]]
```

Creates a capability with the given mimetype and optionally with given
properties. The mimetype can be escaped using `"` or `'`. If you want to
chain caps, you can add more caps in the same format afterwards.

### Caps Properties

```
NAME=[(TYPE)] VALUE in lists and ranges: [(TYPE)] VALUE
```

Sets the requested property in capabilities. The name is an alphanumeric
value and the type can have the following case-insensitive values:

- `i` or `int` for integer values or ranges;
- `f` or `float` for float values or ranges;
- `4` or `fourcc` for FOURCC values;
- `b`, `bool`, or `boolean` for boolean values;
- `s`, `str`, or `string` for strings;
- `fraction` for fractions (framerate, pixel-aspect-ratio);
- `l` or `list` for lists.

If no type was given, the following order is
tried: integer, float, boolean, string. Integer values must be parsable by
`strtol()`, floats by `strtod()`. FOURCC values may either be integers or
strings. Boolean values are (case insensitive) `yes`, `no`, `true` or `false`
and may like strings be escaped with `"` or `'`. Ranges are in this format: `[
VALUE, VALUE]`; lists use this format: `(VALUE [, VALUE ...])`.

## Pipeline Control

A pipeline can be controlled by signals. `SIGUSR2` will stop the pipeline
(`GST_STATE_NULL`); `SIGUSR1` will put it back to play (`GST_STATE_PLAYING`). By
default, the pipeline will start in the `PLAYING` state. There are currently no
signals defined to go into the ready or pause (`GST_STATE_READY` and `GST_STATE_PAUSED`) states explicitly.

## Pipeline Examples

The examples below assume that you have the correct plugins available.
In general, `osssink` can be substituted with another audio output
plugin such as `directsoundsink`, `esdsink`, `alsasink`, `osxaudiosink`, or
`artsdsink`. Likewise, `xvimagesink` can be substituted with `d3dvideosink`,
`ximagesink`, `sdlvideosink`, `osxvideosink`, or `aasink`. Keep in mind though
that different sinks might accept different formats and even the same sink might
accept different formats on different machines, so you might need to add
converter elements like `audioconvert` and `audioresample` for audio or
`videoconvert` in front of the sink to make things work.

### Audio playback

Play the mp3 music file "music.mp3" using a libmad-based plugin and output to
an OSS device:

```
gst-launch-1.0 filesrc location=music.mp3 ! mad ! audioconvert !
audioresample ! osssink
```

Play an Ogg Vorbis format file:

```
gst-launch-1.0 filesrc location=music.ogg ! oggdemux ! vorbisdec !
audioconvert ! audioresample ! osssink
```

Play an mp3 file using GNOME-VFS:

```
gst-launch-1.0 gnomevfssrc location=music.mp3 ! mad ! osssink
```

Play an HTTP stream using GNOME-VFS:

```
gst-launch-1.0 gnomevfssrc location=<http://domain.com/music.mp3> ! mad
! audioconvert ! audioresample ! osssink
```

Use GNOME-VFS to play an mp3 file located on an SMB server:

```
gst-launch-1.0 gnomevfssrc location=<smb://computer/music.mp3> ! mad !
audioconvert ! audioresample ! osssink
```

### Format conversion

Convert an mp3 music file to an Ogg Vorbis file:

```
gst-launch-1.0 filesrc location=music.mp3 ! mad ! audioconvert ! vorbisenc !
oggmux ! filesink location=music.ogg
```

Convert to the FLAC format:

```
gst-launch-1.0 filesrc location=music.mp3 ! mad ! audioconvert ! flacenc !
filesink location=test.flac`
```

### Other

Play a .WAV file that contains raw audio data (PCM):

```
gst-launch-1.0 filesrc location=music.wav ! wavparse ! audioconvert !
audioresample ! osssink
```

Convert a .WAV file containing raw audio data into an Ogg Vorbis or mp3 file:

```
gst-launch-1.0 filesrc location=music.wav ! wavparse ! audioconvert !
vorbisenc ! oggmux ! filesink location=music.ogg
```

```
gst-launch-1.0 filesrc location=music.wav ! wavparse ! audioconvert ! lame !
filesink location=music.mp3
```

Rip all tracks from CD and convert them into a single mp3 file:

```
gst-launch-1.0 cdparanoiasrc mode=continuous ! audioconvert ! lame !
id3v2mux ! filesink location=cd.mp3
```

Rip track 5 from the CD and converts it into a single mp3 file:

```
gst-launch-1.0 cdparanoiasrc track=5 ! audioconvert ! lame ! id3v2mux
! filesink location=track5.mp3
```

Using `gst-inspect-1.0`, it is possible to discover settings like
the above for "cdparanoiasrc" that will tell it to rip the entire CD or
only tracks of it. Alternatively, you can use an URI and `gst-launch-1.0`
will find an element (such as cdparanoia) that supports that protocol
for you, e.g.:

```
gst-launch-1.0 [cdda://5] ! lame vbr=new vbr-quality=6 !
filesink location=track5.mp3
```

Record sound from your audio input and encode it into an ogg file:

```
gst-launch-1.0 osssrc ! audioconvert ! vorbisenc ! oggmux !
filesink location=input.ogg
```

Running a pipeline using a specific user-defined latency
(see gst_pipeline_set_latency()):

```
gst-launch-1.0 pipeline. \( latency=2000000000 videotestsrc ! jpegenc ! jpegdec ! fakevideosink \)
```

### Video

Display only the video portion of an MPEG-1 video file, outputting to an X
display window:

```
gst-launch-1.0 filesrc location=videofile.mpg ! dvddemux ! mpeg2dec !
xvimagesink
```

Display the video portion of a .vob file (used on DVDs), outputting to an SDL
window:

```
gst-launch-1.0 filesrc location=flflfj.vob ! dvddemux ! mpeg2dec ! sdlvideosink
```

Play both video and audio portions of an MPEG movie:

```
gst-launch-1.0 filesrc location=movie.mpg ! dvddemux name=demuxer
demuxer. ! queue ! mpeg2dec ! sdlvideosink
demuxer. ! queue ! mad !  audioconvert ! audioresample ! osssink
```

Play an AVI movie with an external text subtitle stream:

```
gst-launch-1.0 filesrc location=movie.mpg ! mpegdemux name=demuxer
demuxer. ! queue ! mpeg2dec ! videoconvert ! sdlvideosink
demuxer. ! queue ! mad ! audioconvert ! audioresample ! osssink
```

This example shows how to refer to specific pads by name if an
element (here: textoverlay) has multiple sink or source pads:

```
gst-launch-1.0 textoverlay name=overlay ! videoconvert ! videoscale !
autovideosink
filesrc location=movie.avi ! decodebin2 !  videoconvert ! overlay.video_sink
filesrc location=movie.srt ! subparse ! overlay.text_sink
```

Play an AVI movie with an external text subtitle stream using playbin:

```
gst-launch-1.0 playbin uri=<file:///path/to/movie.avi>
suburi=<file:///path/to/movie.srt>
```

### Network streaming

Stream video using RTP and network elements

This command would be run on the transmitter:

```
gst-launch-1.0 v4l2src !
video/x-raw-yuv,width=128,height=96,format='(fourcc)'UYVY !
videoconvert ! ffenc_h263 ! video/x-h263 ! rtph263ppay pt=96 !
udpsink host=192.168.1.1 port=5000 sync=false
```

Use this command on the receiver:

```
gst-launch-1.0 udpsrc port=5000 ! application/x-rtp,
clock-rate=90000,payload=96 ! rtph263pdepay queue-delay=0 ! ffdec_h263
! xvimagesink
```

### Diagnostic

Generate a null stream and ignore it (and print out details):

```
gst-launch-1.0 -v fakesrc num-buffers=16 ! fakesink
```

Generate a pure sine tone to test the audio output:

```
gst-launch-1.0 audiotestsrc ! audioconvert ! audioresample ! osssink
```

Generate a familiar test pattern to test the video output:

```
gst-launch-1.0 videotestsrc ! ximagesink
```

### Automatic linking

You can use the "decodebin" element to automatically select the right
elements to get a working pipeline.

Play any supported audio format:

```
gst-launch-1.0 filesrc location=musicfile ! decodebin ! audioconvert !
audioresample ! osssink
```

Play any supported video format with video and audio output. Threads are used
automatically:

```
gst-launch-1.0 filesrc location=videofile ! decodebin name=decoder
decoder. ! queue ! audioconvert ! audioresample ! osssink
decoder. ! videoconvert ! xvimagesink
```

To make the above even easier, you can use the playbin element:

```
gst-launch-1.0 playbin uri=<file:///home/joe/foo.avi>
```

### Filtered connections

These examples show you how to use filtered caps.

Show a test image and use the YUY2 or YV12 video format for this:

```
gst-launch-1.0 videotestsrc !
'video/x-raw-yuv,format=(fourcc)YUY2;video/x-raw-yuv,format=(fourcc)YV12'
! xvimagesink
```

Record audio and write it to a .wav file. Force usage of signed 16 to 32 bit
samples and a sample rate between 32kHz and 64KHz:

```
gst-launch-1.0 osssrc !
'audio/x-raw-int,rate=[32000,64000],width=[16,32],depth={16,24,32},signed=(boolean)true'
! wavenc ! filesink location=recording.wav
```

## Environment Variables

`GST_DEBUG`: Comma-separated list of debug categories and levels, e.g:

```
GST_DEBUG=totem:4,typefind:5
```

`GST_DEBUG_NO_COLOR`: When this environment variable is set, coloured debug
output is disabled. This might come handy when saving the debug output to a
file.

`GST_DEBUG_DUMP_DOT_DIR`: When set to a filesystem path, store dot
files of pipeline graphs there.

`GST_REGISTRY`: Path of the plugin registry file. The default is
`~/.gstreamer-1.0/registry-CPU.xml` where CPU is the machine/cpu type
GStreamer was compiled for, e.g. 'i486', 'i686', 'x86-64', 'ppc', etc.
Check the output of `uname -i` and `uname -m` for details.

`GST_REGISTRY_UPDATE`: Set to "no" to force GStreamer to assume that no plugins
have changed, have been added or have been removed. This will make GStreamer
skip the initial check to determine whether a rebuild of the registry cache is
required or not. This may be useful in embedded environments where the installed
plugins never change. Do not use this option in any other setup.

`GST_PLUGIN_PATH`: Specifies a list of directories to scan for additional
plugins. These take precedence over the system plugins.

`GST_PLUGIN_SYSTEM_PATH`: Specifies a list of plugins that are always loaded by
default. If not set, this defaults to the system-installed path, and the plugins
installed in the user's home directory

`OIL_CPU_FLAGS`: Useful liboil environment variable. Set `OIL_CPU_FLAGS=0` when
valgrind or other debugging tools trip over liboil's CPU detection. Quite a few
important GStreamer plugins like `videotestsrc`, `audioconvert` and
`audioresample` use liboil.

`G_DEBUG`: This is a useful GLib environment variable. Set
`G_DEBUG=fatal_warnings` to make GStreamer programs abort when a critical
warning such as an assertion failure occurs. This is useful if you want to find
out which part of the code caused that warning to be triggered and under what
circumstances. Simply set `G_DEBUG` as mentioned above and run the program under
gdb (or let it core dump). Then get a stack trace in the usual way.

  [information]: images/icons/emoticons/information.svg
