---
title: Programs
...

# Programs

## `gst-launch`

This is a tool that will construct pipelines based on a command-line
syntax.

A simple commandline looks like:

```
gst-launch filesrc location=hello.mp3 ! mad ! audioresample ! osssink

```

A more complex pipeline looks like:

```
gst-launch filesrc location=redpill.vob ! dvddemux name=demux \
 demux.audio_00 ! queue ! a52dec ! audioconvert ! audioresample ! osssink \
 demux.video_00 ! queue ! mpeg2dec ! videoconvert ! xvimagesink

```

You can also use the parser in you own code. GStreamer provides a
function gst\_parse\_launch () that you can use to construct a pipeline.
The following program lets you create an MP3 pipeline using the
gst\_parse\_launch () function:

``` c
#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  GstElement *filesrc;
  GstMessage *msg;
  GstBus *bus;
  GError *error = NULL;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <filename>\n", argv[0]);
    return -1;
  }

  pipeline = gst_parse_launch ("filesrc name=my_filesrc ! mad ! osssink", &error);
  if (!pipeline) {
    g_print ("Parse error: %s\n", error->message);
    exit (1);
  }

  filesrc = gst_bin_get_by_name (GST_BIN (pipeline), "my_filesrc");
  g_object_set (filesrc, "location", argv[1], NULL);
  g_object_unref (filesrc);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  bus = gst_element_get_bus (pipeline);

  /* wait until we either get an EOS or an ERROR message. Note that in a real
   * program you would probably not use gst_bus_poll(), but rather set up an
   * async signal watch on the bus and run a main loop and connect to the
   * bus's signals to catch certain messages or all messages */
  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS: {
      g_print ("EOS\n");
      break;
    }
    case GST_MESSAGE_ERROR: {
      GError *err = NULL; /* error to show to users                 */
      gchar *dbg = NULL;  /* additional debug string for developers */

      gst_message_parse_error (msg, &err, &dbg);
      if (err) {
        g_printerr ("ERROR: %s\n", err->message);
        g_error_free (err);
      }
      if (dbg) {
        g_printerr ("[Debug details: %s]\n", dbg);
        g_free (dbg);
      }
    }
    default:
      g_printerr ("Unexpected message of type %d", GST_MESSAGE_TYPE (msg));
      break;
  }
  gst_message_unref (msg);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (bus);

  return 0;
}

```

Note how we can retrieve the filesrc element from the constructed bin
using the element name.

### Grammar Reference

The `gst-launch` syntax is processed by a flex/bison parser. This
section is intended to provide a full specification of the grammar; any
deviations from this specification is considered a bug.

#### Elements

```
          ... mad ...

```

A bare identifier (a string beginning with a letter and containing only
letters, numbers, dashes, underscores, percent signs, or colons) will
create an element from a given element factory. In this example, an
instance of the "mad" MP3 decoding plugin will be created.

#### Links

```
          ... !sink ...

```

An exclamation point, optionally having a qualified pad name (an the
name of the pad, optionally preceded by the name of the element) on both
sides, will link two pads. If the source pad is not specified, a source
pad from the immediately preceding element will be automatically chosen.
If the sink pad is not specified, a sink pad from the next element to be
constructed will be chosen. An attempt will be made to find compatible
pads. Pad names may be preceded by an element name, as in
`my_element_name.sink_pad`.

#### Properties

```
          ... location="http://gstreamer.net" ...

```

The name of a property, optionally qualified with an element name, and a
value, separated by an equals sign, will set a property on an element.
If the element is not specified, the previous element is assumed.
Strings can optionally be enclosed in quotation marks. Characters in
strings may be escaped with the backtick (`\`). If the right-hand side
is all digits, it is considered to be an integer. If it is all digits
and a decimal point, it is a double. If it is "true", "false", "TRUE",
or "FALSE" it is considered to be boolean. Otherwise, it is parsed as a
string. The type of the property is determined later on in the parsing,
and the value is converted to the target type. This conversion is not
guaranteed to work, it relies on the g\_value\_convert routines. No
error message will be displayed on an invalid conversion, due to
limitations in the value convert API.

#### Bins, Threads, and Pipelines

```
          ( ... )

```

A pipeline description between parentheses is placed into a bin. The
open paren may be preceded by a type name, as in `jackbin.( ... )` to
make a bin of a specified type. Square brackets make pipelines, and
curly braces make threads. The default toplevel bin type is a pipeline,
although putting the whole description within parentheses or braces can
override this default.

## `gst-inspect`

This is a tool to query a plugin or an element about its properties.

To query the information about the element mad, you would specify:

```
gst-inspect mad

```

Below is the output of a query for the osssink element:

```

Factory Details:
  Rank:         secondary (128)
  Long-name:            Audio Sink (OSS)
  Klass:                Sink/Audio
  Description:          Output to a sound card via OSS
  Author:               Erik Walthinsen <omega@cse.ogi.edu>, Wim Taymans <wim.taymans@chello.be>

Plugin Details:
  Name:                 ossaudio
  Description:          OSS (Open Sound System) support for GStreamer
  Filename:             /home/wim/gst/head/gst-plugins-good/sys/oss/.libs/libgstossaudio.so
  Version:              1.0.0.1
  License:              LGPL
  Source module:        gst-plugins-good
  Source release date:  2012-09-25 12:52 (UTC)
  Binary package:       GStreamer Good Plug-ins git
  Origin URL:           Unknown package origin

GObject
 +----GInitiallyUnowned
       +----GstObject
             +----GstElement
                   +----GstBaseSink
                         +----GstAudioBaseSink
                               +----GstAudioSink
                                     +----GstOssSink

Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      audio/x-raw
                 format: { S16LE, U16LE, S8, U8 }
                 layout: interleaved
                   rate: [ 1, 2147483647 ]
               channels: 1
      audio/x-raw
                 format: { S16LE, U16LE, S8, U8 }
                 layout: interleaved
                   rate: [ 1, 2147483647 ]
               channels: 2
           channel-mask: 0x0000000000000003


Element Flags:
  no flags set

Element Implementation:
  Has change_state() function: gst_audio_base_sink_change_state

Clocking Interaction:
  element is supposed to provide a clock but returned NULL

Element has no indexing capabilities.
Element has no URI handling capabilities.

Pads:
  SINK: 'sink'
    Implementation:
      Has chainfunc(): gst_base_sink_chain
      Has custom eventfunc(): gst_base_sink_event
      Has custom queryfunc(): gst_base_sink_sink_query
      Has custom iterintlinkfunc(): gst_pad_iterate_internal_links_default
    Pad Template: 'sink'

Element Properties:
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "osssink0"
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  sync                : Sync on the clock
                        flags: readable, writable
                        Boolean. Default: true
  max-lateness        : Maximum number of nanoseconds that a buffer can be late before it is dropped (-1 unlimited)
                        flags: readable, writable
                        Integer64. Range: -1 - 9223372036854775807 Default: -1
  qos                 : Generate Quality-of-Service events upstream
                        flags: readable, writable
                        Boolean. Default: false
  async               : Go asynchronously to PAUSED
                        flags: readable, writable
                        Boolean. Default: true
  ts-offset           : Timestamp offset in nanoseconds
                        flags: readable, writable
                        Integer64. Range: -9223372036854775808 - 9223372036854775807 Default: 0
  enable-last-sample  : Enable the last-sample property
                        flags: readable, writable
                        Boolean. Default: false
  last-sample         : The last sample received in the sink
                        flags: readable
                        Boxed pointer of type "GstSample"
  blocksize           : Size in bytes to pull per buffer (0 = default)
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 4096
  render-delay        : Additional render delay of the sink in nanoseconds
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 0
  throttle-time       : The time to keep between rendered buffers
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 0
  buffer-time         : Size of audio buffer in microseconds, this is the minimum latency that the sink reports
                        flags: readable, writable
                        Integer64. Range: 1 - 9223372036854775807 Default: 200000
  latency-time        : The minimum amount of data to write in each iteration in microseconds
                        flags: readable, writable
                        Integer64. Range: 1 - 9223372036854775807 Default: 10000
  provide-clock       : Provide a clock to be used as the global pipeline clock
                        flags: readable, writable
                        Boolean. Default: true
  slave-method        : Algorithm to use to match the rate of the masterclock
                        flags: readable, writable
                        Enum "GstAudioBaseSinkSlaveMethod" Default: 1, "skew"
                           (0): resample         - GST_AUDIO_BASE_SINK_SLAVE_RESAMPLE
                           (1): skew             - GST_AUDIO_BASE_SINK_SLAVE_SKEW
                           (2): none             - GST_AUDIO_BASE_SINK_SLAVE_NONE
  can-activate-pull   : Allow pull-based scheduling
                        flags: readable, writable
                        Boolean. Default: false
  alignment-threshold : Timestamp alignment threshold in nanoseconds
                        flags: readable, writable
                        Unsigned Integer64. Range: 1 - 18446744073709551614 Default: 40000000
  drift-tolerance     : Tolerance for clock drift in microseconds
                        flags: readable, writable
                        Integer64. Range: 1 - 9223372036854775807 Default: 40000
  discont-wait        : Window of time in nanoseconds to wait before creating a discontinuity
                        flags: readable, writable
                        Unsigned Integer64. Range: 0 - 18446744073709551614 Default: 1000000000
  device              : OSS device (usually /dev/dspN)
                        flags: readable, writable
                        String. Default: "/dev/dsp"


```

To query the information about a plugin, you would do:

```
gst-inspect gstelements

```
