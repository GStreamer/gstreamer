---
title: Autoplugging
...

# Autoplugging

In [Your first application][helloworld], you've learned to
build a simple media player for Ogg/Vorbis files. By using alternative
elements, you are able to build media players for other media types,
such as Ogg/Speex, MP3 or even video formats. However, you would rather
want to build an application that can automatically detect the media
type of a stream and automatically generate the best possible pipeline
by looking at all available elements in a system. This process is called
autoplugging, and GStreamer contains high-quality autopluggers. If
you're looking for an autoplugger, don't read any further and go to
[Playback Components][playback-components]. This chapter will
explain the *concept* of autoplugging and typefinding. It will explain
what systems GStreamer includes to dynamically detect the type of a
media stream, and how to generate a pipeline of decoder elements to
playback this media. The same principles can also be used for
transcoding. Because of the full dynamicity of this concept, GStreamer
can be automatically extended to support new media types without needing
any adaptations to its autopluggers.

We will first introduce the concept of Media types as a dynamic and
extendible way of identifying media streams. After that, we will
introduce the concept of typefinding to find the type of a media stream.
Lastly, we will explain how autoplugging and the GStreamer registry can
be used to setup a pipeline that will convert media from one mediatype
to another, for example for media decoding.

[helloworld]: application-development/basics/helloworld.md
[playback-components]: application-development/highlevel/playback-components.md

## Media types as a way to identify streams

We have previously introduced the concept of capabilities as a way for
elements (or, rather, pads) to agree on a media type when streaming data
from one element to the next (see [Capabilities of a pad][pad-caps]). We have
explained that a capability is a combination of a media type and a set of
properties. For most container formats (those are the files that you will find
on your hard disk; Ogg, for example, is a container format), no properties are
needed to describe the stream. Only a media type is needed. A full list
of media types and accompanying properties can be found in [the Plugin
Writer's Guide][pwg-media-types].

An element must associate a media type to its source and sink pads when
it is loaded into the system. GStreamer knows about the different
elements and what type of data they expect and emit through the
GStreamer registry. This allows for very dynamic and extensible element
creation as we will see.

In [Your first application][helloworld], we've learned to
build a music player for Ogg/Vorbis files. Let's look at the media types
associated with each pad in this pipeline. [The Hello world pipeline
with media types](#the-hello-world-pipeline-with-media-types) shows what
media type belongs to each pad in this pipeline.

![The Hello world pipeline with media types](images/mime-world.png "fig:")

Now that we have an idea how GStreamer identifies known media streams,
we can look at methods GStreamer uses to setup pipelines for media
handling and for media type detection.

[pad-caps]: application-development/basics/pads.md#capabilities-of-a-pad
[pwg-media-types]: plugin-development/advanced/media-types.md

## Media stream type detection

Usually, when loading a media stream, the type of the stream is not
known. This means that before we can choose a pipeline to decode the
stream, we first need to detect the stream type. GStreamer uses the
concept of typefinding for this. Typefinding is a normal part of a
pipeline, it will read data for as long as the type of a stream is
unknown. During this period, it will provide data to all plugins that
implement a typefinder. When one of the typefinders recognizes the
stream, the typefind element will emit a signal and act as a passthrough
module from that point on. If no type was found, it will emit an error
and further media processing will stop.

Once the typefind element has found a type, the application can use this
to plug together a pipeline to decode the media stream. This will be
discussed in the next section.

Plugins in GStreamer can, as mentioned before, implement typefinder
functionality. A plugin implementing this functionality will submit a
media type, optionally a set of file extensions commonly used for this
media type, and a typefind function. Once this typefind function inside
the plugin is called, the plugin will see if the data in this media
stream matches a specific pattern that marks the media type identified
by that media type. If it does, it will notify the typefind element of
this fact, telling which mediatype was recognized and how certain we are
that this stream is indeed that mediatype. Once this run has been
completed for all plugins implementing a typefind functionality, the
typefind element will tell the application what kind of media stream it
thinks to have recognized.

The following code should explain how to use the typefind element. It
will print the detected media type, or tell that the media type was not
found. The next section will introduce more useful behaviours, such as
plugging together a decoding pipeline.

```  c
#include <gst/gst.h>

[.. my_bus_callback goes here ..]

static gboolean
idle_exit_loop (gpointer data)
{
  g_main_loop_quit ((GMainLoop *) data);

  /* once */
  return FALSE;
}

static void
cb_typefound (GstElement *typefind,
          guint       probability,
          GstCaps    *caps,
          gpointer    data)
{
  GMainLoop *loop = data;
  gchar *type;

  type = gst_caps_to_string (caps);
  g_print ("Media type %s found, probability %d%%\n", type, probability);
  g_free (type);

  /* since we connect to a signal in the pipeline thread context, we need
   * to set an idle handler to exit the main loop in the mainloop context.
   * Normally, your app should not need to worry about such things. */
  g_idle_add (idle_exit_loop, loop);
}

gint
main (gint   argc,
      gchar *argv[])
{
  GMainLoop *loop;
  GstElement *pipeline, *filesrc, *typefind, *fakesink;
  GstBus *bus;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* check args */
  if (argc != 2) {
    g_print ("Usage: %s <filename>\n", argv[0]);
    return -1;
  }

  /* create a new pipeline to hold the elements */
  pipeline = gst_pipeline_new ("pipe");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, NULL);
  gst_object_unref (bus);

  /* create file source and typefind element */
  filesrc = gst_element_factory_make ("filesrc", "source");
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);
  typefind = gst_element_factory_make ("typefind", "typefinder");
  g_signal_connect (typefind, "have-type", G_CALLBACK (cb_typefound), loop);
  fakesink = gst_element_factory_make ("fakesink", "sink");

  /* setup */
  gst_bin_add_many (GST_BIN (pipeline), filesrc, typefind, fakesink, NULL);
  gst_element_link_many (filesrc, typefind, fakesink, NULL);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* unset */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}

```

Once a media type has been detected, you can plug an element (e.g. a
demuxer or decoder) to the source pad of the typefind element, and
decoding of the media stream will start right after.

## Dynamically autoplugging a pipeline

See [Playback Components][playback-components] for using the
high level object that you can use to dynamically construct pipelines.
