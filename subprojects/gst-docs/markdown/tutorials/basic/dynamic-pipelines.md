#  Basic tutorial 3: Dynamic pipelines


{{ ALERT_PY.md }}

{{ ALERT_JS.md }}

## Goal

This tutorial shows the rest of the basic concepts required to use
GStreamer, which allow building the pipeline "on the fly", as
information becomes available, instead of having a monolithic pipeline
defined at the beginning of your application.

After this tutorial, you will have the necessary knowledge to start the
[Playback tutorials](tutorials/playback/index.md). The points reviewed
here will be:

  - How to attain finer control when linking elements.

  - How to be notified of interesting events so you can react in time.

  - The various states in which an element can be.

## Introduction

As you are about to see, the pipeline in this tutorial is not
completely built before it is set to the playing state. This is OK. If
we did not take further action, data would reach the end of the
pipeline and the pipeline would produce an error message and stop. But
we are going to take further action...

In this example we are opening a file which is multiplexed (or *muxed)*,
this is, audio and video are stored together inside a *container* file.
The elements responsible for opening such containers are called
*demuxers*, and some examples of container formats are Matroska (MKV),
Quick Time (QT, MOV), Ogg, or Advanced Systems Format (ASF, WMV, WMA).

If a container embeds multiple streams (one video and two audio tracks,
for example), the demuxer will separate them and expose them through
different output ports. In this way, different branches can be created
in the pipeline, dealing with different types of data.

The ports through which GStreamer elements communicate with each other
are called pads (`GstPad`). There exists sink pads, through which data
enters an element, and source pads, through which data exits an element.
It follows naturally that source elements only contain source pads, sink
elements only contain sink pads, and filter elements contain
both.

![](images/src-element.png) ![](images/filter-element.png) ![](images/sink-element.png)

**Figure 1**. GStreamer elements with their pads.

A demuxer contains one sink pad, through which the muxed data arrives,
and multiple source pads, one for each stream found in the container:

![](images/filter-element-multi.png)

**Figure 2**. A demuxer with two source pads.

For completeness, here you have a simplified pipeline containing a
demuxer and two branches, one for audio and one for video. This is
**NOT** the pipeline that will be built in this example:

![](images/simple-player.png)

**Figure 3**. Example pipeline with two branches.

The main complexity when dealing with demuxers is that they cannot
produce any information until they have received some data and have had
a chance to look at the container to see what is inside. This is,
demuxers start with no source pads to which other elements can link, and
thus the pipeline must necessarily terminate at them.

The solution is to build the pipeline from the source down to the
demuxer, and set it to run (play). When the demuxer has received enough
information to know about the number and kind of streams in the
container, it will start creating source pads. This is the right time
for us to finish building the pipeline and attach it to the newly added
demuxer pads.

For simplicity, in this example, we will only link to the audio pad and
ignore the video.

## Dynamic Hello World

Copy this code into a text file named `basic-tutorial-3.c` (or find it
in your GStreamer installation).

**basic-tutorial-3.c**

``` c
#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *convert;
  GstElement *resample;
  GstElement *sink;
} CustomData;

/* Handler for the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.source = gst_element_factory_make ("uridecodebin", "source");
  data.convert = gst_element_factory_make ("audioconvert", "convert");
  data.resample = gst_element_factory_make ("audioresample", "resample");
  data.sink = gst_element_factory_make ("autoaudiosink", "sink");

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("test-pipeline");

  if (!data.pipeline || !data.source || !data.convert || !data.resample || !data.sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the pipeline. Note that we are NOT linking the source at this
   * point. We will do it later. */
  gst_bin_add_many (GST_BIN (data.pipeline), data.source, data.convert, data.resample, data.sink, NULL);
  if (!gst_element_link_many (data.convert, data.resample, data.sink, NULL)) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.source, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);

  /* Connect to the pad-added signal */
  g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          }
          break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
    g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}
```

> ![Information](images/icons/emoticons/information.svg)
> Need help?
>
> If you need help to compile this code, refer to the **Building the tutorials**  section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Build), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Build) or [Windows](installing/on-windows.md#InstallingonWindows-Build), or use this specific command on Linux:
> ``gcc basic-tutorial-3.c -o basic-tutorial-3 `pkg-config --cflags --libs gstreamer-1.0` ``
>
>If you need help to run this code, refer to the **Running the tutorials** section for your platform: [Linux](installing/on-linux.md#InstallingonLinux-Run), [Mac OS X](installing/on-mac-osx.md#InstallingonMacOSX-Run) or [Windows](installing/on-windows.md#InstallingonWindows-Run).
>
> This tutorial only plays audio. The media is fetched from the Internet, so it might take a few seconds to start, depending on your connection speed.
>
>Required libraries: `gstreamer-1.0`

## Walkthrough

``` c
/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *convert;
  GstElement *resample;
  GstElement *sink;
} CustomData;
```

So far we have kept all the information we needed (pointers
to `GstElement`s, basically) as local variables. Since this tutorial
(and most real applications) involves callbacks, we will group all our
data in a structure for easier handling.

``` c
/* Handler for the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);
```

This is a forward reference, to be used later.

``` c
/* Create the elements */
data.source = gst_element_factory_make ("uridecodebin", "source");
data.convert = gst_element_factory_make ("audioconvert", "convert");
data.resample = gst_element_factory_make ("audioresample", "resample");
data.sink = gst_element_factory_make ("autoaudiosink", "sink");
```

We create the elements as usual. `uridecodebin` will internally
instantiate all the necessary elements (sources, demuxers and decoders)
to turn a URI into raw audio and/or video streams. It does half the work
that `playbin` does. Since it contains demuxers, its source pads are
not initially available and we will need to link to them on the fly.

`audioconvert` is useful for converting between different audio formats,
making sure that this example will work on any platform, since the
format produced by the audio decoder might not be the same that the
audio sink expects.

`audioresample` is useful for converting between different audio sample rates,
similarly making sure that this example will work on any platform, since the
audio sample rate produced by the audio decoder might not be one that the audio
sink supports.

The `autoaudiosink` is the equivalent of `autovideosink` seen in the
previous tutorial, for audio. It will render the audio stream to the
audio card.

``` c
if (!gst_element_link_many (data.convert, data.resample, data.sink, NULL)) {
  g_printerr ("Elements could not be linked.\n");
  gst_object_unref (data.pipeline);
  return -1;
}
```

Here we link the elements converter, resample and sink, but we **DO NOT** link
them with the source, since at this point it contains no source pads. We
just leave this branch (converter + sink) unlinked, until later on.

``` c
/* Set the URI to play */
g_object_set (data.source, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);
```

We set the URI of the file to play via a property, just like we did in
the previous tutorial.

### Signals

``` c
/* Connect to the pad-added signal */
g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);
```

`GSignals` are a crucial point in GStreamer. They allow you to be
notified (by means of a callback) when something interesting has
happened. Signals are identified by a name, and each `GObject` has its
own signals.

In this line, we are *attaching* to the “pad-added” signal of our source
(an `uridecodebin` element). To do so, we use `g_signal_connect()` and
provide the callback function to be used (`pad_added_handler`) and a
data pointer. GStreamer does nothing with this data pointer, it just
forwards it to the callback so we can share information with it. In this
case, we pass a pointer to the `CustomData` structure we built specially
for this purpose.

The signals that a `GstElement` generates can be found in its
documentation or using the `gst-inspect-1.0` tool as described in [Basic
tutorial 10: GStreamer
tools](tutorials/basic/gstreamer-tools.md).

We are now ready to go! Just set the pipeline to the `PLAYING` state and
start listening to the bus for interesting messages (like `ERROR` or `EOS`),
just like in the previous tutorials.

### The callback

When our source element finally has enough information to start
producing data, it will create source pads, and trigger the “pad-added”
signal. At this point our callback will be
called:

``` c
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
```

`src` is the `GstElement` which triggered the signal. In this example,
it can only be the `uridecodebin`, since it is the only signal to which
we have attached. The first parameter of a signal handler is always the object
that has triggered it.

`new_pad` is the `GstPad` that has just been added to the `src` element.
This is usually the pad to which we want to link.

`data` is the pointer we provided when attaching to the signal. In this
example, we use it to pass the `CustomData` pointer.

``` c
GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
```

From `CustomData` we extract the converter element, and then retrieve
its sink pad using `gst_element_get_static_pad ()`. This is the pad to
which we want to link `new_pad`. In the previous tutorial we linked
element against element, and let GStreamer choose the appropriate pads.
Now we are going to link the pads directly.

``` c
/* If our converter is already linked, we have nothing to do here */
if (gst_pad_is_linked (sink_pad)) {
  g_print ("We are already linked. Ignoring.\n");
  goto exit;
}
```

`uridecodebin` can create as many pads as it sees fit, and for each one,
this callback will be called. These lines of code will prevent us from
trying to link to a new pad once we are already linked.

``` c
/* Check the new pad's type */
new_pad_caps = gst_pad_get_current_caps (new_pad, NULL);
new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
new_pad_type = gst_structure_get_name (new_pad_struct);
if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
  g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
  goto exit;
}
```

Now we will check the type of data this new pad is going to output, because we
are only interested in pads producing audio. We have previously created a
piece of pipeline which deals with audio (an `audioconvert` linked with an
`audioresample` and an `autoaudiosink`), and we will not be able to link it to
a pad producing video, for example.

`gst_pad_get_current_caps()` retrieves the current *capabilities* of the pad
(that is, the kind of data it currently outputs), wrapped in a `GstCaps`
structure. All possible caps a pad can support can be queried with
`gst_pad_query_caps()`. A pad can offer many capabilities, and hence `GstCaps`
can contain many `GstStructure`, each representing a different capability. The
current caps on a pad will always have a single `GstStructure` and represent a
single media format, or if there are no current caps yet `NULL` will be
returned.

Since, in this case, we know that the pad we want only had one
capability (audio), we retrieve the first `GstStructure` with
`gst_caps_get_structure()`.

Finally, with `gst_structure_get_name()` we recover the name of the
structure, which contains the main description of the format (its *media
type*, actually).

If the name is not `audio/x-raw`, this is not a decoded
audio pad, and we are not interested in it.

Otherwise, attempt the link:

``` c
/* Attempt the link */
ret = gst_pad_link (new_pad, sink_pad);
if (GST_PAD_LINK_FAILED (ret)) {
  g_print ("Type is '%s' but link failed.\n", new_pad_type);
} else {
  g_print ("Link succeeded (type '%s').\n", new_pad_type);
}
```

`gst_pad_link()` tries to link two pads. As it was the case
with `gst_element_link()`, the link must be specified from source to
sink, and both pads must be owned by elements residing in the same bin
(or pipeline).

And we are done! When a pad of the right kind appears, it will be
linked to the rest of the audio-processing pipeline and execution will
continue until ERROR or EOS. However, we will squeeze a bit more content
from this tutorial by also introducing the concept of State.

### GStreamer States

We already talked a bit about states when we said that playback does not
start until you bring the pipeline to the `PLAYING` state. We will
introduce here the rest of states and their meaning. There are 4 states
in GStreamer:

| State     | Description |
|-----------|--------------------|
| `NULL`    | the NULL state or initial state of an element. |
| `READY`   | the element is ready to go to PAUSED. |
| `PAUSED`  | the element is PAUSED, it is ready to accept and process data. Sink elements however only accept one buffer and then block. |
| `PLAYING` | the element is PLAYING, the clock is running and the data is flowing. |


You can only move between adjacent ones, this is, you can't go from `NULL`
to `PLAYING`, you have to go through the intermediate `READY` and `PAUSED`
states. If you set the pipeline to `PLAYING`, though, GStreamer will make
the intermediate transitions for you.

``` c
case GST_MESSAGE_STATE_CHANGED:
  /* We are only interested in state-changed messages from the pipeline */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
    g_print ("Pipeline state changed from %s to %s:\n",
        gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
  }
  break;
```

We added this piece of code that listens to bus messages regarding state
changes and prints them on screen to help you understand the
transitions. Every element puts messages on the bus regarding its
current state, so we filter them out and only listen to messages coming
from the pipeline.

Most applications only need to worry about going to `PLAYING` to start
playback, then to `PAUSED` to perform a pause, and then back to `NULL` at
program exit to free all resources.

## Exercise

Dynamic pad linking has traditionally been a difficult topic for a lot
of programmers. Prove that you have achieved its mastery by
instantiating an `autovideosink` (probably with an `videoconvert` in
front) and link it to the demuxer when the right pad appears. Hint: You
are already printing on screen the type of the video pads.

You should now see (and hear) the same movie as in [Basic tutorial 1:
Hello world!](tutorials/basic/hello-world.md). In
that tutorial you used `playbin`, which is a handy element that
automatically takes care of all the demuxing and pad linking for you.
Most of the [Playback tutorials](tutorials/playback/index.md) are devoted
to `playbin`.

## Conclusion

In this tutorial, you learned:

  - How to be notified of events using `GSignals`
  - How to connect `GstPad`s directly instead of their parent elements.
  - The various states of a GStreamer element.

You also combined these items to build a dynamic pipeline, which was not
defined at program start, but was created as information regarding the
media was available.

You can now continue with the basic tutorials and learn about performing
seeks and time-related queries in [Basic tutorial 4: Time
management](tutorials/basic/time-management.md) or move
to the [Playback tutorials](tutorials/playback/index.md), and gain more
insight about the `playbin` element.

Remember that attached to this page you should find the complete source
code of the tutorial and any accessory files needed to build it.
It has been a pleasure having you here, and see you soon!
