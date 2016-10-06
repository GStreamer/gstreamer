# gst-launch-1.0

> ![information] This is the Linux man page for
> the `gst-inspect-1.0` tool. As such, it is very Linux-centric
> regarding path specification and plugin names. Please be patient while
> it is rewritten to be more generic.

## Name

gst-launch-1.0 - build and run a GStreamer pipeline

## Synopsis

**gst-launch-1.0** *\[OPTION...\]* PIPELINE-DESCRIPTION

## Description

*gst-launch-1.0* is a tool that builds and runs
basic *GStreamer* pipelines.

In simple form, a PIPELINE-DESCRIPTION is a list of elements separated
by exclamation marks (!). Properties may be appended to elements, in the
form*property=value*.

For a complete description of possible PIPELINE-DESCRIPTIONS see the
section*pipeline description* below or consult the GStreamer
documentation.

Please note that *gst-launch-1.0* is primarily a debugging tool for
developers and users. You should not build applications on top of it.
For applications, use the gst\_parse\_launch() function of the GStreamer
API as an easy way to construct pipelines from pipeline descriptions.

## Options

*gst-launch-1.0* accepts the following options:

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

 

## Gstreamer Options

*gst-launch-1.0* also accepts the following options that are common to
all GStreamer applications:

## Pipeline Description

A pipeline consists *elements* and *links*. *Elements* can be put
into *bins* of different sorts. *Elements*, *links* and *bins* can be
specified in a pipeline description in any order.

**Elements**

ELEMENTTYPE *\[PROPERTY1 ...\]*

Creates an element of type ELEMENTTYPE and sets the PROPERTIES.

**Properties**

PROPERTY=VALUE ...

Sets the property to the specified value. You can
use **gst-inspect-1.0**(1) to find out about properties and allowed
values of different elements. Enumeration properties can be set by name,
nick or value.

**Bins**

*\[BINTYPE.\]* ( *\[PROPERTY1 ...\]* PIPELINE-DESCRIPTION )

Specifies that a bin of type BINTYPE is created and the given properties
are set. Every element between the braces is put into the bin. Please
note the dot that has to be used after the BINTYPE. You will almost
never need this functionality, it is only really useful for applications
using the gst\_launch\_parse() API with 'bin' as bintype. That way it is
possible to build partial pipelines instead of a full-fledged top-level
pipeline.

**Links**

*\[\[SRCELEMENT\].\[PAD1,...\]\]* ! *\[\[SINKELEMENT\].\[PAD1,...\]\]
\[\[SRCELEMENT\].\[PAD1,...\]\]* ! CAPS
! *\[\[SINKELEMENT\].\[PAD1,...\]\]*

Links the element with name SRCELEMENT to the element with name
SINKELEMENT, using the caps specified in CAPS as a filter. Names can be
set on elements with the name property. If the name is omitted, the
element that was specified directly in front of or after the link is
used. This works across bins. If a padname is given, the link is done
with these pads. If no pad names are given all possibilities are tried
and a matching pad is used. If multiple padnames are given, both sides
must have the same number of pads specified and multiple links are done
in the given order. So the simplest link is a simple exclamation mark,
that links the element to the left of it to the element right of it.

**Caps**

MIMETYPE *\[, PROPERTY\[, PROPERTY ...\]\]\] \[; CAPS\[; CAPS ...\]\]*

Creates a capability with the given mimetype and optionally with given
properties. The mimetype can be escaped using " or '. If you want to
chain caps, you can add more caps in the same format afterwards.

**Properties**

NAME=*\[(TYPE)\]*VALUE in lists and ranges: *\[(TYPE)\]*VALUE

Sets the requested property in capabilities. The name is an alphanumeric
value and the type can have the following case-insensitive values:
- **i** or **int** for integer values or ranges - **f** or **float** for
float values or ranges - **4** or **fourcc** for FOURCC values
- **b**, **bool** or **boolean** for boolean values
- **s**, **str** or **string** for strings - **fraction** for fractions
(framerate, pixel-aspect-ratio) - **l** or **list** for lists If no type
was given, the following order is tried: integer, float, boolean,
string. Integer values must be parsable by **strtol()**, floats
by **strtod()**. FOURCC values may either be integers or strings.
Boolean values are (case insensitive) *yes*, *no*, *true* or *false* and
may like strings be escaped with " or '. Ranges are in this format: \[
VALUE, VALUE \] Lists use this format: ( VALUE *\[, VALUE ...\]* )

## Pipeline Control

A pipeline can be controlled by signals. SIGUSR2 will stop the pipeline
(GST\_STATE\_NULL); SIGUSR1 will put it back to play
(GST\_STATE\_PLAYING). By default, the pipeline will start in the
playing state. There are currently no signals defined to go into the
ready or pause (GST\_STATE\_READY and GST\_STATE\_PAUSED) state
explicitely.

## Pipeline Examples

The examples below assume that you have the correct plug-ins available.
In general, "osssink" can be substituted with another audio output
plug-in such as "directsoundsink", "esdsink", "alsasink",
"osxaudiosink", or "artsdsink". Likewise, "xvimagesink" can be
substituted with "d3dvideosink", "ximagesink", "sdlvideosink",
"osxvideosink", or "aasink". Keep in mind though that different sinks
might accept different formats and even the same sink might accept
different formats on different machines, so you might need to add
converter elements like audioconvert and audioresample (for audio) or
videoconvert (for video) in front of the sink to make things work.

**Audio playback**

`gst-launch-1.0 filesrc location=music.mp3 ! mad ! audioconvert !
audioresample ! osssink` Play the mp3 music file "music.mp3" using a
libmad-based plug-in and output to an OSS device

`gst-launch-1.0 filesrc location=music.ogg ! oggdemux ! vorbisdec !
audioconvert ! audioresample ! osssink` Play an Ogg Vorbis format file

`gst-launch-1.0 gnomevfssrc location=music.mp3 ! mad ! osssink
gst-launch-1.0 gnomevfssrc location=<http://domain.com/music.mp3> ! mad
! audioconvert ! audioresample ! osssink` Play an mp3 file or an http
stream using GNOME-VFS

`gst-launch-1.0 gnomevfssrc location=<smb://computer/music.mp3> ! mad !
audioconvert ! audioresample ! osssink` Use GNOME-VFS to play an mp3
file located on an SMB server

**Format conversion**

`gst-launch-1.0 filesrc location=music.mp3 ! mad ! audioconvert !
vorbisenc ! oggmux ! filesink location=music.ogg` Convert an mp3 music
file to an Ogg Vorbis file

`gst-launch-1.0 filesrc location=music.mp3 ! mad ! audioconvert !
flacenc ! filesink location=test.flac` Convert to the FLAC format

**Other**

`gst-launch-1.0 filesrc location=music.wav ! wavparse ! audioconvert !
audioresample ! osssink` Plays a .WAV file that contains raw audio data
(PCM).

`gst-launch-1.0 filesrc location=music.wav ! wavparse ! audioconvert !
vorbisenc ! oggmux ! filesink location=music.ogg gst-launch-1.0 filesrc
location=music.wav ! wavparse ! audioconvert ! lame ! filesink
location=music.mp3` Convert a .WAV file containing raw audio data into
an Ogg Vorbis or mp3 file

`gst-launch-1.0 cdparanoiasrc mode=continuous ! audioconvert ! lame !
id3v2mux ! filesink location=cd.mp3` rips all tracks from compact disc
and convert them into a single mp3 file

`gst-launch-1.0 cdparanoiasrc track=5 ! audioconvert ! lame ! id3v2mux
! filesink location=track5.mp3` rips track 5 from the CD and converts
it into a single mp3 file

Using **gst-inspect-1.0**(1), it is possible to discover settings like
the above for cdparanoiasrc that will tell it to rip the entire cd or
only tracks of it. Alternatively, you can use an URI and gst-launch-1.0
will find an element (such as cdparanoia) that supports that protocol
for you, e.g.: `gst-launch-1.0 \[cdda://5\] ! lame vbr=new
vbr-quality=6 ! filesink location=track5.mp3`

`gst-launch-1.0 osssrc ! audioconvert ! vorbisenc ! oggmux ! filesink
location=input.ogg` records sound from your audio input and encodes it
into an ogg file

**Video**

`gst-launch-1.0 filesrc location=JB\_FF9\_TheGravityOfLove.mpg !
dvddemux ! mpeg2dec ! xvimagesink` Display only the video portion of an
MPEG-1 video file, outputting to an X display window

`gst-launch-1.0 filesrc location=/flflfj.vob ! dvddemux ! mpeg2dec !
sdlvideosink` Display the video portion of a .vob file (used on DVDs),
outputting to an SDL window

`gst-launch-1.0 filesrc location=movie.mpg ! dvddemux name=demuxer
demuxer. ! queue ! mpeg2dec ! sdlvideosink demuxer. ! queue ! mad !
audioconvert ! audioresample ! osssink` Play both video and audio
portions of an MPEG movie

`gst-launch-1.0 filesrc location=movie.mpg ! mpegdemux name=demuxer
demuxer. ! queue ! mpeg2dec ! videoconvert ! sdlvideosink demuxer. !
queue ! mad ! audioconvert ! audioresample ! osssink` Play an AVI movie
with an external text subtitle stream

This example also shows how to refer to specific pads by name if an
element (here: textoverlay) has multiple sink or source pads.

`gst-launch-1.0 textoverlay name=overlay ! videoconvert !
videoscale ! autovideosink filesrc location=movie.avi ! decodebin2 !
videoconvert ! overlay.video\_sink filesrc location=movie.srt !
subparse ! overlay.text\_sink`

Play an AVI movie with an external text subtitle stream using playbin

`gst-launch-1.0 playbin uri=<file:///path/to/movie.avi>
suburi=<file:///path/to/movie.srt>`

**Network streaming**

Stream video using RTP and network elements.

`gst-launch-1.0 v4l2src !
video/x-raw-yuv,width=128,height=96,format='(fourcc)'UYVY !
videoconvert ! ffenc\_h263 ! video/x-h263 ! rtph263ppay pt=96 !
udpsink host=192.168.1.1 port=5000 sync=false` Use this command on the
receiver

`gst-launch-1.0 udpsrc port=5000 ! application/x-rtp,
clock-rate=90000,payload=96 ! rtph263pdepay queue-delay=0 ! ffdec\_h263
! xvimagesink` This command would be run on the transmitter

**Diagnostic**

`gst-launch-1.0 -v fakesrc num-buffers=16 ! fakesink` Generate a null
stream and ignore it (and print out details).

`gst-launch-1.0 audiotestsrc ! audioconvert ! audioresample ! osssink`
Generate a pure sine tone to test the audio output

`gst-launch-1.0 videotestsrc ! xvimagesink gst-launch-1.0 videotestsrc
! ximagesink` Generate a familiar test pattern to test the video output

**Automatic linking**

You can use the decodebin element to automatically select the right
elements to get a working pipeline.

`gst-launch-1.0 filesrc location=musicfile ! decodebin ! audioconvert !
audioresample ! osssink` Play any supported audio format

`gst-launch-1.0 filesrc location=videofile ! decodebin name=decoder
decoder. ! queue ! audioconvert ! audioresample ! osssink decoder. !
videoconvert ! xvimagesink` Play any supported video format with
video and audio output. Threads are used automatically. To make this
even easier, you can use the playbin element:

`gst-launch-1.0 playbin uri=<file:///home/joe/foo.avi>`

**Filtered connections**

These examples show you how to use filtered caps.

`gst-launch-1.0 videotestsrc !
'video/x-raw-yuv,format=(fourcc)YUY2;video/x-raw-yuv,format=(fourcc)YV12'
! xvimagesink` Show a test image and use the YUY2 or YV12 video format
for this.

`gst-launch-1.0 osssrc !
'audio/x-raw-int,rate=\[32000,64000\],width=\[16,32\],depth={16,24,32},signed=(boolean)true'
! wavenc ! filesink location=recording.wav` record audio and write it
to a .wav file. Force usage of signed 16 to 32 bit samples and a sample
rate between 32kHz and 64KHz.

## Environment Variables

`GST\_DEBUG`: Comma-separated list of debug categories and levels,
e.g. GST\_DEBUG= totem:4,typefind:5

`GST\_DEBUG\_NO\_COLOR`: When this environment variable is set,
coloured debug output is disabled.

`GST\_DEBUG\_DUMP\_DOT\_DIR`: When set to a filesystem path, store dot
files of pipeline graphs there.

`GST\_REGISTRY`: Path of the plugin registry file. Default is
\~/.gstreamer-1.0/registry-CPU.xml where CPU is the machine/cpu type
GStreamer was compiled for, e.g. 'i486', 'i686', 'x86-64', 'ppc', etc.
(check the output of "uname -i" and "uname -m" for details).

`GST\_REGISTRY\_UPDATE`: Set to "no" to force GStreamer to assume that
no plugins have changed, been added or been removed. This will make
GStreamer skip the initial check whether a rebuild of the registry cache
is required or not. This may be useful in embedded environments where
the installed plugins never change. Do not use this option in any other
setup.

`GST\_PLUGIN\_PATH`: Specifies a list of directories to scan for
additional plugins. These take precedence over the system plugins.

`GST\_PLUGIN\_SYSTEM\_PATH`: Specifies a list of plugins that are
always loaded by default. If not set, this defaults to the
system-installed path, and the plugins installed in the user's home
directory

`OIL\_CPU\_FLAGS`: Useful liboil environment variable. Set
OIL\_CPU\_FLAGS=0 when valgrind or other debugging tools trip over
liboil's CPU detection (quite a few important GStreamer plugins like
videotestsrc, audioconvert or audioresample use liboil).

`G\_DEBUG`: Useful GLib environment variable. Set
G\_DEBUG=fatal\_warnings to make GStreamer programs abort when a
critical warning such as an assertion failure occurs. This is useful if
you want to find out which part of the code caused that warning to be
triggered and under what circumstances. Simply set G\_DEBUG as mentioned
above and run the program in gdb (or let it core dump). Then get a stack
trace in the usual way

  [information]: images/icons/emoticons/information.png
