---
title: Playback Components
...

# Playback Components

GStreamer includes several higher-level components to simplify an
application developer's life. All of the components discussed here (for
now) are targetted at media playback. The idea of each of these
components is to integrate as closely as possible with a GStreamer
pipeline, but to hide the complexity of media type detection and several
other rather complex topics that have been discussed in [Advanced
GStreamer concepts][advanced].

We currently recommend people to use either playbin (see
[Playbin](#playbin)) or decodebin (see [Decodebin](#decodebin)),
depending on their needs. Playbin is the recommended solution for
everything related to simple playback of media that should just work.
Decodebin is a more flexible autoplugger that could be used to add more
advanced features, such as playlist support, crossfading of audio tracks
and so on. Its programming interface is more low-level than that of
playbin, though.

[advanced]: application-development/advanced/index.md

## Playbin

Playbin is an element that can be created using the standard GStreamer
API (e.g. `gst_element_factory_make ()`). The factory is conveniently
called “playbin”. By being a `GstPipeline` (and thus a `GstElement`),
playbin automatically supports all of the features of this class,
including error handling, tag support, state handling, getting stream
positions, seeking, and so on.

Setting up a playbin pipeline is as simple as creating an instance of
the playbin element, setting a file location using the “uri” property on
playbin, and then setting the element to the `GST_STATE_PLAYING` state
(the location has to be a valid URI, so “\<protocol\>://\<location\>”,
e.g. file:///tmp/my.ogg or http://www.example.org/stream.ogg).
Internally, playbin will set up a pipeline to playback the media
location.

``` c
#include <gst/gst.h>

[.. my_bus_callback goes here ..]

gint
main (gint   argc,
      gchar *argv[])
{
  GMainLoop *loop;
  GstElement *play;
  GstBus *bus;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* make sure we have a URI */
  if (argc != 2) {
    g_print ("Usage: %s <URI>\n", argv[0]);
    return -1;
  }

  /* set up */
  play = gst_element_factory_make ("playbin", "play");
  g_object_set (G_OBJECT (play), "uri", argv[1], NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (play));
  gst_bus_add_watch (bus, my_bus_callback, loop);
  gst_object_unref (bus);

  gst_element_set_state (play, GST_STATE_PLAYING);

  /* now run */
  g_main_loop_run (loop);

  /* also clean up */
  gst_element_set_state (play, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (play));

  return 0;
}

```

Playbin has several features that have been discussed previously:

  - Settable video and audio output (using the “video-sink” and
    “audio-sink” properties).

  - Mostly controllable and trackable as a `GstElement`, including error
    handling, eos handling, tag handling, state handling (through the
    `GstBus`), media position handling and seeking.

  - Buffers network-sources, with buffer fullness notifications being
    passed through the `GstBus`.

  - Supports visualizations for audio-only media.

  - Supports subtitles, both in the media as well as from separate
    files. For separate subtitle files, use the “suburi” property.

  - Supports stream selection and disabling. If your media has multiple
    audio or subtitle tracks, you can dynamically choose which one to
    play back, or decide to turn it off altogether (which is especially
    useful to turn off subtitles). For each of those, use the
    “current-text” and other related properties.

For convenience, it is possible to test “playbin” on the commandline,
using the command “gst-launch-1.0 playbin uri=file:///path/to/file”.

## Decodebin

Decodebin is the actual autoplugger backend of playbin, which was
discussed in the previous section. Decodebin will, in short, accept
input from a source that is linked to its sinkpad and will try to detect
the media type contained in the stream, and set up decoder routines for
each of those. It will automatically select decoders. For each decoded
stream, it will emit the “pad-added” signal, to let the client know
about the newly found decoded stream. For unknown streams (which might
be the whole stream), it will emit the “unknown-type” signal. The
application is then responsible for reporting the error to the user.

``` c

#include <gst/gst.h>


[.. my_bus_callback goes here ..]



GstElement *pipeline, *audio;

static void
cb_newpad (GstElement *decodebin,
       GstPad     *pad,
       gpointer    data)
{
  GstCaps *caps;
  GstStructure *str;
  GstPad *audiopad;

  /* only link once */
  audiopad = gst_element_get_static_pad (audio, "sink");
  if (GST_PAD_IS_LINKED (audiopad)) {
    g_object_unref (audiopad);
    return;
  }

  /* check media type */
  caps = gst_pad_query_caps (pad, NULL);
  str = gst_caps_get_structure (caps, 0);
  if (!g_strrstr (gst_structure_get_name (str), "audio")) {
    gst_caps_unref (caps);
    gst_object_unref (audiopad);
    return;
  }
  gst_caps_unref (caps);

  /* link'n'play */
  gst_pad_link (pad, audiopad);

  g_object_unref (audiopad);
}

gint
main (gint   argc,
      gchar *argv[])
{
  GMainLoop *loop;
  GstElement *src, *dec, *conv, *sink;
  GstPad *audiopad;
  GstBus *bus;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* make sure we have input */
  if (argc != 2) {
    g_print ("Usage: %s <filename>\n", argv[0]);
    return -1;
  }

  /* setup */
  pipeline = gst_pipeline_new ("pipeline");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, loop);
  gst_object_unref (bus);

  src = gst_element_factory_make ("filesrc", "source");
  g_object_set (G_OBJECT (src), "location", argv[1], NULL);
  dec = gst_element_factory_make ("decodebin", "decoder");
  g_signal_connect (dec, "pad-added", G_CALLBACK (cb_newpad), NULL);
  gst_bin_add_many (GST_BIN (pipeline), src, dec, NULL);
  gst_element_link (src, dec);

  /* create audio output */
  audio = gst_bin_new ("audiobin");
  conv = gst_element_factory_make ("audioconvert", "aconv");
  audiopad = gst_element_get_static_pad (conv, "sink");
  sink = gst_element_factory_make ("alsasink", "sink");
  gst_bin_add_many (GST_BIN (audio), conv, sink, NULL);
  gst_element_link (conv, sink);
  gst_element_add_pad (audio,
      gst_ghost_pad_new ("sink", audiopad));
  gst_object_unref (audiopad);
  gst_bin_add (GST_BIN (pipeline), audio);

  /* run */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* cleanup */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}


```

Decodebin, similar to playbin, supports the following features:

  - Can decode an unlimited number of contained streams to decoded
    output pads.

  - Is handled as a `GstElement` in all ways, including tag or error
    forwarding and state handling.

Although decodebin is a good autoplugger, there's a whole lot of things
that it does not do and is not intended to do:

  - Taking care of input streams with a known media type (e.g. a DVD, an
    audio-CD or such).

  - Selection of streams (e.g. which audio track to play in case of
    multi-language media streams).

  - Overlaying subtitles over a decoded video stream.

Decodebin can be easily tested on the commandline, e.g. by using the
command `gst-launch-1.0 filesrc location=file.ogg ! decodebin
! audioconvert ! audioresample ! autoaudiosink`.

## URIDecodebin

The uridecodebin element is very similar to decodebin, only that it
automatically plugs a source plugin based on the protocol of the URI
given.

Uridecodebin will also automatically insert buffering elements when the
uri is a slow network source. The buffering element will post BUFFERING
messages that the application needs to handle as explained in
[Buffering][buffering]. The following properties can be used
to configure the buffering method:

  - The buffer-size property allows you to configure a maximum size in
    bytes for the buffer element.

  - The buffer-duration property allows you to configure a maximum size
    in time for the buffer element. The time will be estimated based on
    the bitrate of the network.

  - With the download property you can enable the download buffering method
    as described in [Download buffering][download-buffering]. Setting this
    option to TRUE will only enable download buffering for selected
    formats such as quicktime, flash video, avi and webm.

  - You can also enable buffering on the parsed/demuxed data with the
    use-buffering property. This is interesting to enable buffering on
    slower random access media such as a network file server.

URIDecodebin can be easily tested on the commandline, e.g. by using the
command `gst-launch-1.0 uridecodebin uri=file:///file.ogg !
! audioconvert ! audioresample ! autoaudiosink`.

[buffering]: application-development/advanced/buffering.md
[download-buffering]: application-development/advanced/buffering.md#download-buffering

## Playsink

The playsink element is a powerful sink element. It has request pads for
raw decoded audio, video and text and it will configure itself to play
the media streams. It has the following features:

  - It exposes GstStreamVolume, GstVideoOverlay, GstNavigation and
    GstColorBalance interfaces and automatically plugs software elements
    to implement the interfaces when needed.

  - It will automatically plug conversion elements.

  - Can optionally render visualizations when there is no video input.

  - Configurable sink elements.

  - Configurable audio/video sync offset to fine-tune synchronization in
    badly muxed files.

  - Support for taking a snapshot of the last video frame.

Below is an example of how you can use playsink. We use a uridecodebin
element to decode into raw audio and video streams which we then link to
the playsink request pads. We only link the first audio and video pads,
you could use an input-selector to link all pads.

``` c


#include <gst/gst.h>


[.. my_bus_callback goes here ..]





GstElement *pipeline, *sink;

static void
cb_pad_added (GstElement *dec,
          GstPad     *pad,
          gpointer    data)
{
  GstCaps *caps;
  GstStructure *str;
  const gchar *name;
  GstPadTemplate *templ;
  GstElementClass *klass;

  /* check media type */
  caps = gst_pad_query_caps (pad, NULL);
  str = gst_caps_get_structure (caps, 0);
  name = gst_structure_get_name (str);

  klass = GST_ELEMENT_GET_CLASS (sink);

  if (g_str_has_prefix (name, "audio")) {
    templ = gst_element_class_get_pad_template (klass, "audio_sink");
  } else if (g_str_has_prefix (name, "video")) {
    templ = gst_element_class_get_pad_template (klass, "video_sink");
  } else if (g_str_has_prefix (name, "text")) {
    templ = gst_element_class_get_pad_template (klass, "text_sink");
  } else {
    templ = NULL;
  }

  if (templ) {
    GstPad *sinkpad;

    sinkpad = gst_element_request_pad (sink, templ, NULL, NULL);

    if (!gst_pad_is_linked (sinkpad))
      gst_pad_link (pad, sinkpad);

    gst_object_unref (sinkpad);
  }

  gst_clear_caps (&caps);
}

gint
main (gint   argc,
      gchar *argv[])
{
  GMainLoop *loop;
  GstElement *dec;
  GstBus *bus;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* make sure we have input */
  if (argc != 2) {
    g_print ("Usage: %s <uri>\n", argv[0]);
    return -1;
  }

  /* setup */
  pipeline = gst_pipeline_new ("pipeline");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, loop);
  gst_object_unref (bus);

  dec = gst_element_factory_make ("uridecodebin", "source");
  g_object_set (G_OBJECT (dec), "uri", argv[1], NULL);
  g_signal_connect (dec, "pad-added", G_CALLBACK (cb_pad_added), NULL);

  /* create audio output */
  sink = gst_element_factory_make ("playsink", "sink");
  gst_util_set_object_arg (G_OBJECT (sink), "flags",
      "soft-colorbalance+soft-volume+vis+text+audio+video");
  gst_bin_add_many (GST_BIN (pipeline), dec, sink, NULL);

  /* run */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* cleanup */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}



```

This example will show audio and video depending on what you give it.
Try this example on an audio file and you will see that it shows
visualizations. You can change the visualization at runtime by changing
the vis-plugin property.
