# Playback tutorial 7: Custom playbin sinks


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

`playbin` can be further customized by manually selecting its audio and
video sinks. This allows applications to rely on `playbin` to retrieve
and decode the media and then manage the final render/display
themselves. This tutorial shows:

  - How to replace the sinks selected by `playbin`.
  - How to use a complex pipeline as a sink.

## Introduction

Two properties of `playbin` allow selecting the desired audio and video
sinks: `audio-sink` and `video-sink` (respectively). The application
only needs to instantiate the appropriate `GstElement` and pass it to
`playbin` through these properties.

This method, though, only allows using a single Element as sink. If a
more complex pipeline is required, for example, an equalizer plus an
audio sink, it needs to be wrapped in a Bin, so it looks to
`playbin` as if it was a single Element.

A Bin (`GstBin`) is a container that encapsulates partial pipelines so
they can be managed as single elements. As an example, the
`GstPipeline` we have been using in all tutorials is a type of
`GstBin`, which does not interact with external Elements. Elements
inside a Bin connect to external elements through Ghost Pads
(`GstGhostPad`), this is, Pads on the surface of the Bin which simply
forward data from an external Pad to a given Pad on an internal Element.

![](images/bin-element-ghost.png)

**Figure 1:** A Bin with two Elements and one Ghost Pad.

`GstBin`s are also a type of `GstElement`, so they can be used wherever
an Element is required, in particular, as sinks for `playbin` (and they
are then known as **sink-bins**).

## An equalized player

Copy this code into a text file named `playback-tutorial-7.c`.

**playback-tutorial7.c**

``` c
#include <gst/gst.h>

int main(int argc, char *argv[]) {
  GstElement *pipeline, *bin, *equalizer, *convert, *sink;
  GstPad *pad, *ghost_pad;
  GstBus *bus;
  GstMessage *msg;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Build the pipeline */
  pipeline = gst_parse_launch ("playbin uri=https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);

  /* Create the elements inside the sink bin */
  equalizer = gst_element_factory_make ("equalizer-3bands", "equalizer");
  convert = gst_element_factory_make ("audioconvert", "convert");
  sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
  if (!equalizer || !convert || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Create the sink bin, add the elements and link them */
  bin = gst_bin_new ("audio_sink_bin");
  gst_bin_add_many (GST_BIN (bin), equalizer, convert, sink, NULL);
  gst_element_link_many (equalizer, convert, sink, NULL);
  pad = gst_element_get_static_pad (equalizer, "sink");
  ghost_pad = gst_ghost_pad_new ("sink", pad);
  gst_pad_set_active (ghost_pad, TRUE);
  gst_element_add_pad (bin, ghost_pad);
  gst_object_unref (pad);

  /* Configure the equalizer */
  g_object_set (G_OBJECT (equalizer), "band1", (gdouble)-24.0, NULL);
  g_object_set (G_OBJECT (equalizer), "band2", (gdouble)-24.0, NULL);

  /* Set playbin's audio sink to be our sink bin */
  g_object_set (GST_OBJECT (pipeline), "audio-sink", bin, NULL);

  /* Start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
```

> ![information] If you need help to compile this code, refer to the
> **Building the tutorials** section for your platform: [Mac] or
> [Windows] or use this specific command on Linux:
>
> `` gcc playback-tutorial-7.c -o playback-tutorial-7 `pkg-config --cflags --libs gstreamer-1.0` ``
>
> If you need help to run this code, refer to the **Running the
> tutorials** section for your platform: [Mac OS X], [Windows][1], for
> [iOS] or for [android].
>
> This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed. The higher frequency bands have been attenuated, so the movie sound should have a more powerful bass component.<
>
> Required libraries: `gstreamer-1.0`

## Walkthrough

``` c
/* Create the elements inside the sink bin */
equalizer = gst_element_factory_make ("equalizer-3bands", "equalizer");
convert = gst_element_factory_make ("audioconvert", "convert");
sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
if (!equalizer || !convert || !sink) {
  g_printerr ("Not all elements could be created.\n");
  return -1;
}
```

All the Elements that compose our sink-bin are instantiated. We use an
`equalizer-3bands` and an `autoaudiosink`, with an `audioconvert` in
between, because we are not sure of the capabilities of the audio sink
(since they are hardware-dependant).

``` c
/* Create the sink bin, add the elements and link them */
bin = gst_bin_new ("audio_sink_bin");
gst_bin_add_many (GST_BIN (bin), equalizer, convert, sink, NULL);
gst_element_link_many (equalizer, convert, sink, NULL);
```

This adds the new Elements to the Bin and links them just as we would do
if this was a pipeline.

``` c
pad = gst_element_get_static_pad (equalizer, "sink");
ghost_pad = gst_ghost_pad_new ("sink", pad);
gst_pad_set_active (ghost_pad, TRUE);
gst_element_add_pad (bin, ghost_pad);
gst_object_unref (pad);
```

Now we need to create a Ghost Pad so this partial pipeline inside the
Bin can be connected to the outside. This Ghost Pad will be connected to
a Pad in one of the internal Elements (the sink pad of the equalizer),
so we retrieve this Pad with `gst_element_get_static_pad()`. Remember
from [](tutorials/basic/multithreading-and-pad-availability.md) that
if this was a Request Pad instead of an Always Pad, we would need to use
`gst_element_request_pad()`.

The Ghost Pad is created with `gst_ghost_pad_new()` (pointing to the
inner Pad we just acquired), and activated with `gst_pad_set_active()`.
It is then added to the Bin with `gst_element_add_pad()`, transferring
ownership of the Ghost Pad to the bin, so we do not have to worry about
releasing it.

Finally, the sink Pad we obtained from the equalizer needs to be release
with `gst_object_unref()`.

At this point, we have a functional sink-bin, which we can use as the
audio sink in `playbin`. We just need to instruct `playbin` to use it:

``` c
/* Set playbin's audio sink to be our sink bin */
g_object_set (GST_OBJECT (pipeline), "audio-sink", bin, NULL);
```

It is as simple as setting the `audio-sink` property on `playbin` to
the newly created sink.

``` c
/* Configure the equalizer */
g_object_set (G_OBJECT (equalizer), "band1", (gdouble)-24.0, NULL);
g_object_set (G_OBJECT (equalizer), "band2", (gdouble)-24.0, NULL);
```

The only bit remaining is to configure the equalizer. For this example,
the two higher frequency bands are set to the maximum attenuation so the
bass is boosted. Play a bit with the values to feel the difference (Look
at the documentation for the `equalizer-3bands` element for the allowed
range of values).

## Exercise

Build a video bin instead of an audio bin, using one of the many
interesting video filters GStreamer offers, like `solarize`,
`vertigotv` or any of the Elements in the `effectv` plugin. Remember to
use the color space conversion element `videoconvert` if your
pipeline fails to link due to incompatible caps.

## Conclusion

This tutorial has shown:

  - How to set your own sinks to `playbin` using the audio-sink and
    video-sink properties.
  - How to wrap a piece of pipeline into a `GstBin` so it can be used as
    a **sink-bin** by `playbin`.

It has been a pleasure having you here, and see you soon\!

  [information]: images/icons/emoticons/information.svg
  [Mac]: installing/on-mac-osx.md
  [Windows]: installing/on-windows.md
  [Mac OS X]: installing/on-mac-osx.md#building-the-tutorials
  [1]: installing/on-windows.md#running-the-tutorials
  [iOS]: installing/for-ios-development.md#building-the-tutorials
  [android]: installing/for-android-development.md#building-the-tutorials
  [warning]: images/icons/emoticons/warning.svg
