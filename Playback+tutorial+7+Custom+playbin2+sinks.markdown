# Playback tutorial 7: Custom playbin2 sinks

This page last changed on Dec 03, 2012 by xartigas.

# Goal

`playbin2` can be further customized by manually selecting its audio and
video sinks. This allows applications to rely on `playbin2` to retrieve
and decode the media and then manage the final render/display
themselves. This tutorial shows:

  - How to replace the sinks selected by `playbin2`.
  - How to use a complex pipeline as a sink.

# Introduction

Two properties of `playbin2` allow selecting the desired audio and video
sinks: `audio-sink` and `video-sink` (respectively). The application
only needs to instantiate the appropriate `GstElement` and pass it to
`playbin2` through these properties.

This method, though, only allows using a single Element as sink. If a
more complex pipeline is required, for example, an equalizer plus an
audio sink, it needs to be wrapped in a Bin, so it looks to
`playbin2` as if it was a single Element.

A Bin (`GstBin`) is a container that encapsulates partial pipelines so
they can be managed as single elements. As an example, the
`GstPipeline` we have been using in all tutorials is a type of
`GstBin`, which does not interact with external Elements. Elements
inside a Bin connect to external elements through Ghost Pads
(`GstGhostPad`), this is, Pads on the surface of the Bin which simply
forward data from an external Pad to a given Pad on an internal Element.

![](attachments/1441842/2424880.png)

**Figure 1:** A Bin with two Elements and one Ghost Pad.

`GstBin`s are also a type of `GstElement`, so they can be used wherever
an Element is required, in particular, as sinks for `playbin2` (and they
are then known as **sink-bins**).

# An equalized player

Copy this code into a text file named `playback-tutorial-7.c`.

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><p>This tutorial is included in the SDK since release 2012.7. If you cannot find it in the downloaded code, please install the latest release of the GStreamer SDK.</p></td>
</tr>
</tbody>
</table>

**playback-tutorial7.c**

``` lang=c
#include <gst/gst.h>

int main(int argc, char *argv[]) {
  GstElement *pipeline, *bin, *equalizer, *convert, *sink;
  GstPad *pad, *ghost_pad;
  GstBus *bus;
  GstMessage *msg;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Build the pipeline */
  pipeline = gst_parse_launch ("playbin2 uri=http://docs.gstreamer.com/media/sintel_trailer-480p.webm", NULL);

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

  /* Set playbin2's audio sink to be our sink bin */
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

<table>
<tbody>
<tr class="odd">
<td><img src="images/icons/emoticons/information.png" width="16" height="16" /></td>
<td><div id="expander-1371267928" class="expand-container">
<div id="expander-control-1371267928" class="expand-control">
<span class="expand-control-icon"><img src="images/icons/grey_arrow_down.gif" class="expand-control-image" /></span><span class="expand-control-text">Need help? (Click to expand)</span>
</div>
<div id="expander-content-1371267928" class="expand-content">
<p>If you need help to compile this code, refer to the <strong>Building the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Build">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Build">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Build">Windows</a>, or use this specific command on Linux:</p>
<div class="panel" style="border-width: 1px;">
<div class="panelContent">
<p><code>gcc playback-tutorial-7.c -o playback-tutorial-7 `pkg-config --cflags --libs gstreamer-0.10`</code></p>
</div>
</div>
<p>If you need help to run this code, refer to the <strong>Running the tutorials</strong> section for your platform: <a href="Installing%2Bon%2BLinux.html#InstallingonLinux-Run">Linux</a>, <a href="Installing%2Bon%2BMac%2BOS%2BX.html#InstallingonMacOSX-Run">Mac OS X</a> or <a href="Installing%2Bon%2BWindows.html#InstallingonWindows-Run">Windows</a></p>
<p><span>This tutorial opens a window and displays a movie, with accompanying audio. The media is fetched from the Internet, so the window might take a few seconds to appear, depending on your connection speed. The higher frequency bands have been attenuated, so the movie sound should have a more powerful bass component.</span></p>
<p>Required libraries: <code>gstreamer-0.10</code></p>
</div>
</div></td>
</tr>
</tbody>
</table>

# Walkthrough

``` lang=c
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
`equalizer-3bands` and an `autoaudiosink`, with an `audioconvert` in
between, because we are not sure of the capabilities of the audio sink
(since they are hardware-dependant).

``` lang=c
/* Create the sink bin, add the elements and link them */
bin = gst_bin_new ("audio_sink_bin");
gst_bin_add_many (GST_BIN (bin), equalizer, convert, sink, NULL);
gst_element_link_many (equalizer, convert, sink, NULL);
```

This adds the new Elements to the Bin and links them just as we would do
if this was a pipeline.

``` lang=c
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
from [Basic tutorial 7: Multithreading and Pad
Availability](Basic%2Btutorial%2B7%253A%2BMultithreading%2Band%2BPad%2BAvailability.html) that
if this was a Request Pad instead of an Always Pad, we would need to use
`gst_element_request_pad()`.

The Ghost Pad is created with `gst_ghost_pad_new()` (pointing to the
inner Pad we just acquired), and activated with `gst_pad_set_active()`.
It is then added to the Bin with `gst_element_add_pad()`, transferring
ownership of the Ghost Pad to the bin, so we do not have to worry about
releasing it.

Finally, the sink Pad we obtained from the equalizer needs to be release
with `gst_object_unref()`.

At this point, we have a functional sink-bin, which we can use as the
audio sink in `playbin2`. We just need to instruct `playbin2` to use it:

``` lang=c
/* Set playbin2's audio sink to be our sink bin */
g_object_set (GST_OBJECT (pipeline), "audio-sink", bin, NULL);
```

It is as simple as setting the `audio-sink` property on `playbin2` to
the newly created sink.

``` lang=c
/* Configure the equalizer */
g_object_set (G_OBJECT (equalizer), "band1", (gdouble)-24.0, NULL);
g_object_set (G_OBJECT (equalizer), "band2", (gdouble)-24.0, NULL);
```

The only bit remaining is to configure the equalizer. For this example,
the two higher frequency bands are set to the maximum attenuation so the
bass is boosted. Play a bit with the values to feel the difference (Look
at the documentation for the `equalizer-3bands` element for the allowed
range of values).

# Exercise

Build a video bin instead of an audio bin, using one of the many
interesting video filters GStreamer offers, like `solarize`,
`vertigotv` or any of the Elements in the `effectv` plugin. Remember to
use the color space conversion element `ffmpegcolorspace` if your
pipeline fails to link due to incompatible caps.

# Conclusion

This tutorial has shown:

  - How to set your own sinks to `playbin2` using the audio-sink and
    video-sink properties.
  - How to wrap a piece of pipeline into a `GstBin` so it can be used as
    a **sink-bin** by `playbin2`.

It has been a pleasure having you here, and see you soon\!

## Attachments:

![](images/icons/bullet_blue.gif)
[bin-element-ghost.png](attachments/1441842/2424880.png) (image/png)
![](images/icons/bullet_blue.gif)
[playback-tutorial-7.c](attachments/1441842/2424881.c) (text/plain)
![](images/icons/bullet_blue.gif)
[vs2010.zip](attachments/1441842/2424882.zip) (application/zip)

Document generated by Confluence on Oct 08, 2015 10:27
